#include "firmware_flasher.h"
#include "common/elevated_install.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QResource>
#include <QTemporaryDir>
#include <QVector>
#include <algorithm>

FirmwareFlasher::FirmwareFlasher(QObject* parent) : QObject(parent)
{
    QFile manifestFile(":/firmware/manifest.json");
    if (!manifestFile.open(QIODevice::ReadOnly))
        return;

    releaseManifest_ = QJsonDocument::fromJson(manifestFile.readAll()).object();
    releaseImages_ = releaseManifest_.value("files").toArray();
    useBundledFirmware();

    connect(&process_, &QProcess::readyReadStandardOutput, this,
            [this] { consumeOutput(process_.readAllStandardOutput()); });
    connect(&process_, &QProcess::readyReadStandardError, this,
            [this] { consumeOutput(process_.readAllStandardError()); });
    connect(&process_, &QProcess::finished, this, &FirmwareFlasher::finish);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            setError("Could not start the bundled ESP flasher.");
    });
}

FirmwareFlasher::~FirmwareFlasher() = default;

void FirmwareFlasher::useBundledFirmware()
{
    manifest_ = releaseManifest_;
    images_ = releaseImages_;
    customBuildLoaded_ = false;
    customBuildName_.clear();
    qint64 totalBytes = 0;
    for (const auto& image : images_)
        totalBytes += image.toObject().value("size").toInteger();
    firmwareSize_ = QString::number(totalBytes / 1024.0, 'f', 0) + " KB";
    error_.clear();
    selectionError_.clear();
    status_ = "Ready to flash";
    emit changed();
}

bool FirmwareFlasher::loadCustomBuild(const QUrl& manifestUrl)
{
    if (flashing())
        return false;
    selectionError_.clear();
    error_.clear();
    const QString manifestPath =
        manifestUrl.isLocalFile() ? manifestUrl.toLocalFile() : manifestUrl.toString();
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        selectionError_ = "Could not open the selected ESP-IDF flasher_args.json.";
        status_ = "Custom build not loaded";
        emit changed();
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        selectionError_ = "The selected file is not a valid ESP-IDF flasher_args.json.";
        status_ = "Custom build not loaded";
        emit changed();
        return false;
    }

    const auto source = document.object();
    const auto extra = source.value("extra_esptool_args").toObject();
    const auto settings = source.value("flash_settings").toObject();
    const auto flashFiles = source.value("flash_files").toObject();
    const QString chip = extra.value("chip").toString();
    const QString flashMode = settings.value("flash_mode").toString();
    const QString flashSize = settings.value("flash_size").toString();
    const QString flashFreq = settings.value("flash_freq").toString();
    if (chip != "esp32c5" || flashFiles.size() < 3 || flashMode.isEmpty() || flashSize.isEmpty() ||
        flashFreq.isEmpty())
    {
        selectionError_ = "Choose a complete ESP-IDF build for the ESP32-C5 (flasher_args.json).";
        status_ = "Custom build not loaded";
        emit changed();
        return false;
    }
    const QStringList requiredComponents{"bootloader", "partition-table", "app"};
    for (const auto& component : requiredComponents)
    {
        const auto image = source.value(component).toObject();
        const QString offset = image.value("offset").toString();
        const QString fileName = image.value("file").toString();
        if (offset.isEmpty() || fileName.isEmpty() ||
            flashFiles.value(offset).toString() != fileName)
        {
            selectionError_ =
                "The selected build must include its bootloader, partition table, and app.";
            status_ = "Custom build not loaded";
            emit changed();
            return false;
        }
    }

    const QString buildRoot = QFileInfo(manifestPath).absolutePath();
    const QString canonicalRoot = QFileInfo(buildRoot).canonicalFilePath();
    QVector<QPair<quint64, QJsonObject>> orderedImages;
    qint64 totalBytes = 0;
    QRegularExpression offsetPattern(R"(^0x[0-9a-fA-F]+$)");
    for (auto it = flashFiles.begin(); it != flashFiles.end(); ++it)
    {
        const QString offsetText = it.key();
        const QString relativeName = it.value().toString();
        bool offsetOk = false;
        offsetText.toULongLong(&offsetOk, 16);
        if (!offsetOk || !offsetPattern.match(offsetText).hasMatch() || relativeName.isEmpty())
        {
            selectionError_ = "The ESP-IDF build contains an invalid flash image entry.";
            status_ = "Custom build not loaded";
            emit changed();
            return false;
        }

        const QFileInfo imageInfo(QDir(buildRoot).filePath(relativeName));
        const QString imagePath = imageInfo.canonicalFilePath();
        if (!imageInfo.isFile() || imageInfo.size() <= 0 || imagePath.isEmpty() ||
            (!canonicalRoot.isEmpty() &&
             !imagePath.startsWith(canonicalRoot + '/', Qt::CaseInsensitive)))
        {
            selectionError_ =
                "A firmware image is missing or outside the selected ESP-IDF build folder.";
            status_ = "Custom build not loaded";
            emit changed();
            return false;
        }

        QJsonObject image;
        image.insert("offset", offsetText);
        image.insert("file", imageInfo.fileName());
        image.insert("path", imagePath);
        image.insert("size", imageInfo.size());
        orderedImages.append({offsetText.toULongLong(nullptr, 16), image});
        totalBytes += imageInfo.size();
    }

    std::sort(orderedImages.begin(), orderedImages.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    QJsonArray customImages;
    for (const auto& image : orderedImages)
        customImages.append(image.second);

    manifest_ = QJsonObject{{"chip", chip},
                            {"baud", 460800},
                            {"before", extra.value("before").toString("default_reset")},
                            {"after", extra.value("after").toString("hard_reset")},
                            {"flashMode", flashMode},
                            {"flashSize", flashSize},
                            {"flashFreq", flashFreq}};
    images_ = customImages;
    firmwareSize_ = QString::number(totalBytes / 1024.0, 'f', 0) + " KB";
    const QString projectName = QFileInfo(buildRoot).dir().dirName();
    customBuildName_ =
        projectName.isEmpty() ? "Custom ESP-IDF build" : projectName + " · custom build";
    customBuildLoaded_ = true;
    error_.clear();
    selectionError_.clear();
    status_ = "Custom build ready to flash";
    emit changed();
    return true;
}

void FirmwareFlasher::flash(const QString& port)
{
    if (flashing() || port.isEmpty())
        return;
    temporary_ = std::make_unique<QTemporaryDir>();
    if (!temporary_->isValid())
    {
        setError("Could not create a temporary folder for flashing.");
        return;
    }

    const QString toolPath = temporary_->filePath("esptool.exe");
    if (!unpackResource(":/firmware/esptool.exe", toolPath))
    {
        setError("Could not extract the bundled ESP flasher.");
        return;
    }

    QStringList arguments{"--chip",
                          manifest_.value("chip").toString(),
                          "--port",
                          port,
                          "--baud",
                          QString::number(manifest_.value("baud").toInt()),
                          "--before",
                          manifest_.value("before").toString(),
                          "--after",
                          manifest_.value("after").toString(),
                          "write_flash",
                          "--flash_mode",
                          manifest_.value("flashMode").toString(),
                          "--flash_size",
                          manifest_.value("flashSize").toString(),
                          "--flash_freq",
                          manifest_.value("flashFreq").toString()};
    for (const auto& image : images_)
    {
        const auto entry = image.toObject();
        const QString fileName = entry.value("file").toString();
        QString imagePath;
        if (customBuildLoaded_)
        {
            imagePath = entry.value("path").toString();
            if (!QFileInfo::exists(imagePath))
            {
                setError("A selected custom firmware image is no longer available: " + fileName);
                return;
            }
        } else
        {
            imagePath = temporary_->filePath(fileName);
            if (!unpackResource(":/firmware/" + fileName, imagePath))
            {
                setError("Could not extract firmware image: " + fileName);
                return;
            }
        }
        arguments << entry.value("offset").toString() << imagePath;
    }

    progress_ = 0;
    error_.clear();
    outputTail_.clear();
    status_ = "Connecting to ESP32-C5…";
    process_.setProgram(toolPath);
    process_.setArguments(arguments);
    process_.setWorkingDirectory(temporary_->path());
    process_.start();
    emit changed();
}

void FirmwareFlasher::consumeOutput(const QByteArray& output)
{
    const QString text = QString::fromLocal8Bit(output);
    outputTail_ += text;
    if (outputTail_.size() > 2000)
        outputTail_.remove(0, outputTail_.size() - 2000);
    const QRegularExpression progressPattern(R"(Writing at (0x[0-9a-fA-F]+).*?\((\d+)\s*%\))");
    auto match = progressPattern.globalMatch(text);
    while (match.hasNext())
    {
        const auto result = match.next();
        bool offsetOk = false;
        const auto offset = result.captured(1).toULongLong(&offsetOk, 16);
        const int imagePercent = result.captured(2).toInt();
        if (!offsetOk)
            continue;

        qint64 completed = 0, total = 0, currentSize = 0;
        bool foundCurrent = false;
        for (const auto& image : images_)
        {
            const auto entry = image.toObject();
            const auto imageOffset = entry.value("offset").toString().toULongLong(nullptr, 16);
            const auto imageSize = entry.value("size").toInteger();
            total += imageSize;
            if (offset >= imageOffset)
            {
                completed += currentSize;
                currentSize = imageSize;
                foundCurrent = true;
            }
        }
        if (foundCurrent && total > 0)
        {
            progress_ = qBound(
                progress_, int((completed + currentSize * imagePercent / 100) * 100 / total), 99);
            status_ = "Writing firmware…";
            emit changed();
        }
    }

    if (text.contains("Connecting") || text.contains("Detecting"))
    {
        status_ = "Connecting to ESP32-C5…";
        emit changed();
    }
}

void FirmwareFlasher::setError(const QString& message)
{
    error_ = message;
    status_ = "Flash failed";
    emit changed();
    emit finished(false);
}

void FirmwareFlasher::finish(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
    if (success)
    {
        progress_ = 100;
        status_ = "Flash complete · ESP32-C5 restarting";
        error_.clear();
    } else
    {
        const QString output = outputTail_.trimmed();
        error_ = output.isEmpty() ? "ESP32-C5 flashing failed. Check the connection and try again."
                                  : output.right(700);
        status_ = "Flash failed";
    }
    temporary_.reset();
    emit changed();
    emit finished(success);
}
