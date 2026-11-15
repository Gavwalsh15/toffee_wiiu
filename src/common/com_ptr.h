#pragma once

template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com()
    {
        if (p)
            p->Release();
    }
    T** put()
    {
        if (p)
            p->Release();
        p = nullptr;
        return &p;
    }
    T* operator->() const { return p; }
};
