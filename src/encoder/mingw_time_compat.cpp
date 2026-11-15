#include <sys/timeb.h>
extern "C" int ftime64(struct __timeb64* value)
{
    _ftime64(value);
    return 0;
}
