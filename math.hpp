#pragma once

#include <math.h>
#include <assert.h>

template<typename T>
inline T max(T a, T b) { return a > b ? a : b; }

template<typename T>
inline T min(T a, T b) { return a < b ? a : b; }

template<typename T>
struct tvec3;

template<typename T>
struct tvec2;

template<typename T>
struct tvec4
{
    // todo: create from vec3, vec2, ...
    // the same for vec3
    tvec4() = default;
    explicit tvec4(T v) : x(v), y(v), z(v), w(v) {}
    tvec4(T x, T y, T z, T w) : x(x), y(y), z(z), w(w) {}
    tvec4(const tvec3<T>& v, T w);
    tvec4(T x, const tvec3<T>& v);
    tvec4(const tvec2<T>& v1, const tvec2<T>& v2);
    tvec4(const tvec2<T>& v, T z, T w);
    tvec4(T x, T y, const tvec2<T>& v);

    template<typename U>
    explicit tvec4(tvec4<U> v) : x(v.x), y(v.y), z(v.z), w(v.w) {}

    //                @ const tvec4& ?
    tvec4& operator+=(tvec4 v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
    tvec4& operator-=(tvec4 v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
    tvec4& operator*=(tvec4 v) { x *= v.x; y *= v.y; z *= v.z; w *= v.w; return *this; }
    tvec4& operator*=(T v) { x *= v; y *= v; z *= v; w *= v; return *this; }
    tvec4& operator/=(tvec4 v) { x /= v.x; y /= v.y; z /= v.z; w /= v.w;  return *this; }
    tvec4& operator/=(T v) { x /= v; y /= v; z /= v; w /= v; return *this; }

    tvec4 operator+(tvec4 v) const { return { x + v.x, y + v.y, z + v.z, w + v.w }; }
    tvec4 operator-(tvec4 v) const { return { x - v.x, y - v.y, z - v.z, w - v.w }; }
    tvec4 operator-()        const { return { -x, -y, -z, -w }; }
    tvec4 operator*(tvec4 v) const { return { x * v.x, y * v.y, z * v.z, w * v.w }; }
    tvec4 operator*(T v)     const { return { x * v, y * v, z * v, w * v }; }
    tvec4 operator/(tvec4 v) const { return { x / v.x, y / v.y, z / v.z, w / v.w }; }
    tvec4 operator/(T v)     const { return { x / v, y / v, z / v, w / v }; }

    const T&     operator[](int idx)const { return *(&x + idx); }
    T&     operator[](int idx) { return *(&x + idx); }

    bool operator==(tvec4 v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
    bool operator!=(tvec4 v) const { return !(*this == v); }

    T x;
    T y;
    T z;
    T w;
};

template<typename T>
inline tvec4<T> operator*(T scalar, tvec4<T> v) { return v * scalar; }

using ivec4 = tvec4<int>;
using vec4 = tvec4<float>;

template<typename T>
struct tvec3
{
    tvec3() = default;
    explicit tvec3(T v) : x(v), y(v), z(v) {}
    tvec3(T x, T y, T z) : x(x), y(y), z(z) {}
    tvec3(T x, const tvec2<T>& v);
    tvec3(const tvec2<T>& v, T z);

    template<typename U>
    explicit tvec3(tvec3<U> v) : x(v.x), y(v.y), z(v.z) {}

    template<typename U>
    explicit tvec3(tvec4<U> v) : x(v.x), y(v.y), z(v.z) {}

    //                @ const tvec3& ?
    tvec3& operator+=(tvec3 v) { x += v.x; y += v.y; z += v.z; return *this; }
    tvec3& operator-=(tvec3 v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    tvec3& operator*=(tvec3 v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
    tvec3& operator*=(T v) { x *= v; y *= v; z *= v; return *this; }
    tvec3& operator/=(tvec3 v) { x /= v.x; y /= v.y; z /= v.z; return *this; }
    tvec3& operator/=(T v) { x /= v; y /= v; z /= v; return *this; }

    tvec3 operator+(tvec3 v) const { return { x + v.x, y + v.y, z + v.z }; }
    tvec3 operator-(tvec3 v) const { return { x - v.x, y - v.y, z - v.z }; }
    tvec3 operator-()        const { return { -x, -y, -z }; }
    tvec3 operator*(tvec3 v) const { return { x * v.x, y * v.y, z * v.z }; }
    tvec3 operator*(T v)     const { return { x * v, y * v, z * v }; }
    tvec3 operator/(tvec3 v) const { return { x / v.x, y / v.y, z / v.z }; }
    tvec3 operator/(T v)     const { return { x / v, y / v, z / v }; }

    const T&     operator[](int idx)const { return *(&x + idx); }
    T&     operator[](int idx) { return *(&x + idx); }

    bool operator==(tvec3 v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(tvec3 v) const { return !(*this == v); }

    T x;
    T y;
    T z;
};

template<typename T>
inline tvec3<T> operator*(T scalar, tvec3<T> v) { return v * scalar; }

using ivec3 = tvec3<int>;
using vec3 = tvec3<float>;

template<typename T>
struct tvec2
{
    tvec2() = default;
    explicit tvec2(T v) : x(v), y(v) {}
    tvec2(T x, T y) : x(x), y(y) {}

    template<typename U>
    explicit tvec2(tvec2<U> v) : x(v.x), y(v.y) {}

    template<typename U>
    explicit tvec2(tvec4<U> v) : x(v.x), y(v.y) {}

    template<typename U>
    explicit tvec2(tvec3<U> v) : x(v.x), y(v.y) {}

    //                @ const tvec2& ?
    tvec2& operator+=(tvec2 v) { x += v.x; y += v.y; return *this; }
    tvec2& operator-=(tvec2 v) { x -= v.x; y -= v.y; return *this; }
    tvec2& operator*=(tvec2 v) { x *= v.x; y *= v.y; return *this; }
    tvec2& operator*=(T v) { x *= v; y *= v; return *this; }
    tvec2& operator/=(tvec2 v) { x /= v.x; y /= v.y; return *this; }
    tvec2& operator/=(T v) { x /= v; y /= v; return *this; }

    tvec2 operator+(tvec2 v) const { return { x + v.x, y + v.y }; }
    tvec2 operator-(tvec2 v) const { return { x - v.x, y - v.y }; }
    tvec2 operator-()        const { return { -x, -y }; }
    tvec2 operator*(tvec2 v) const { return { x * v.x, y * v.y }; }
    tvec2 operator*(T v)     const { return { x * v, y * v }; }
    tvec2 operator/(tvec2 v) const { return { x / v.x, y / v.y }; }
    tvec2 operator/(T v)     const { return { x / v, y / v }; }

    const T&     operator[](int idx)const { return *(&x + idx); }
    T&     operator[](int idx) { return *(&x + idx); }

    bool operator==(tvec2 v) const { return x == v.x && y == v.y; }
    bool operator!=(tvec2 v) const { return !(*this == v); }

    T x;
    T y;
};

template<typename T>
inline tvec2<T> operator*(T scalar, tvec2<T> v) { return v * scalar; }

using ivec2 = tvec2<int>;
using vec2 = tvec2<float>;

template<typename T>
inline tvec4<T>::tvec4(const tvec3<T>& v, T w) : x(v.x), y(v.y), z(v.z), w(w) {}

template<typename T>
inline tvec4<T>::tvec4(T x, const tvec3<T>& v) : x(x), y(v.x), z(v.y), w(v.z) {}

template<typename T>
inline tvec4<T>::tvec4(const tvec2<T>& v1, const tvec2<T>& v2) : x(v1.x), y(v1.y), z(v2.x), w(v2.y) {}

template<typename T>
inline tvec4<T>::tvec4(const tvec2<T>& v, T z, T w) : x(v.x), y(v.y), z(z), w(w) {}

template<typename T>
inline tvec4<T>::tvec4(T x, T y, const tvec2<T>& v) : x(x), y(y), z(v.x), w(v.y) {}

template<typename T>
inline tvec3<T>::tvec3(T x, const tvec2<T>& v) : x(x), y(v.x), z(v.y) {}

template<typename T>
inline tvec3<T>::tvec3(const tvec2<T>& v, T z) : x(v.x), y(v.y), z(z) {}

inline vec3 cross(vec3 v, vec3 w)
{
    return  {
        v.y * w.z - v.z * w.y,
                v.z * w.x - v.x * w.z,
                v.x * w.y - v.y * w.x
    };
}

inline float lerp(float x, float y, float a)
{
    return x + (y - x) * a;
}

inline float length(vec2 v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

inline float length(vec3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline vec2 normalize(vec2 v)
{
    return v * (1.f / length(v));
}

inline vec3 normalize(vec3 v)
{
    return v * (1.f / length(v));
}

inline float dot(vec3 v1, vec3 v2)
{
    return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
}

inline float dot(vec2 v1, vec2 v2)
{
    return v1.x * v2.x + v1.y * v2.y;
}

// n must be normalized
inline vec3 reflect(vec3 toReflect, vec3 n)
{
    return dot(toReflect, n) * n * -2.f + toReflect;
}

struct mat3
{
    mat3() = default;
    mat3(vec3 i, vec3 j, vec3 k) :
        i(i),
        j(j),
        k(k)
    {}

    vec3 i = { 1.f, 0.f, 0.f };
    vec3 j = { 0.f, 1.f, 0.f };
    vec3 k = { 0.f, 0.f, 1.f };

    vec3& operator[](int idx) { return *(&i + idx); }
    const vec3& operator[](int idx) const { return *(&i + idx); }
};

inline vec3 operator*(const mat3& m, vec3 v)
{
    return v.x * m.i + v.y * m.j + v.z * m.k;
}

inline mat3 operator*(const mat3& lhs, const mat3& rhs)
{
    return
            mat3{
        lhs * rhs.i,
                lhs * rhs.j,
                lhs * rhs.k
    };
}

struct mat4
{
    vec4 i = { 1.f, 0.f, 0.f, 0.f };
    vec4 j = { 0.f, 1.f, 0.f, 0.f };
    vec4 k = { 0.f, 0.f, 1.f, 0.f };
    vec4 w = { 0.f, 0.f, 0.f, 1.f };

    vec4& operator[](int idx) { return *(&i + idx); }
    const vec4& operator[](int idx) const { return *(&i + idx); }
};

inline vec4 operator*(const mat4& m, vec4 v)
{
    return m.i * v.x + m.j * v.y + m.k * v.z + m.w * v.w;
}

inline mat4 operator*(const mat4& ml, const mat4& mr)
{
    mat4 m;
    m.i = ml * mr.i;
    m.j = ml * mr.j;
    m.k = ml * mr.k;
    m.w = ml * mr.w;
    return m;
}

inline mat4 translate(vec3 v)
{
    mat4 m;
    m.w = vec4(v, 1.f);
    return m;
}

inline mat4 scale(vec3 scale)
{
    mat4 m;
    m.i.x = scale.x;
    m.j.y = scale.y;
    m.k.z = scale.z;
    return m;
}

inline mat4 transpose(const mat4& m)
{
    mat4 mt;
    mt.i = { m.i.x, m.j.x, m.k.x, m.w.x };
    mt.j = { m.i.y, m.j.y, m.k.y, m.w.y };
    mt.k = { m.i.z, m.j.z, m.k.z, m.w.z };
    mt.w = { m.i.w, m.j.w, m.k.w, m.w.w };
    return mt;
}

inline mat4 lookAt(vec3 pos, vec3 target, vec3 up)
{
    vec3 k = -normalize(target - pos);
    vec3 i = normalize(cross(up, k));
    vec3 j = cross(k, i);

    mat4 basis;
    basis.i = vec4(i, 0.f);
    basis.j = vec4(j, 0.f);
    basis.k = vec4(k, 0.f);

    // change of a basis matrix is orthogonal (i, j, k, h are unit vectors and are perpendicular)
    // so inverse equals transpose (but I don't know the details)
    mat4 bInverse = transpose(basis);
    return bInverse * translate(-pos);
}

// windows...
#undef near
#undef far

// songho.ca/opengl/gl_projectionmatrix.html

inline mat4 frustum(float right, float top, float near, float far)
{
    assert(near > 0.f);
    assert(far > near);

    mat4 m;
    m.i = { near / right, 0.f, 0.f, 0.f };
    m.j = { 0.f, near / top, 0.f, 0.f };
    m.k = { 0.f, 0.f, -(far + near) / (far - near), -1.f };
    m.w = { 0.f, 0.f, (-2.f * far * near) / (far - near), 0.f };
    return m;
}

#define PI 3.14159265359f

inline float toRadians(float degrees)
{
    return degrees / 360.f * 2.f * PI;
}

// fovy is in degrees
inline mat4 perspective(float fovy, float aspect, float near, float far)
{
    fovy = toRadians(fovy);
    float top = tanf(fovy) * near;
    float right = aspect * top;
    return frustum(right, top, near, far);
}


inline mat4 orthographic(float left, float right, float bottom, float top, float near,
        float far)
{
    mat4 mat;

    mat.i = {2.f / (right - left), 0.f, 0.f, 0.f};
    mat.j = {0.f, 2.f / (top - bottom), 0.f, 0.f};
    mat.k = {0.f, 0.f, -2.f / (far - near), 0.f};
    mat.w = {-(right + left) / (right - left), -(top + bottom) / (top - bottom),
            -(far + near) / (far - near), 1.f};

    return mat;
}

// todo: 3x3 matrices, rotation around any axis
// in degrees
inline mat4 rotateY(float angle)
{
    const float rad = toRadians(angle);
    const float sin = sinf(rad);
    const float cos = cosf(rad);
    mat4 m;
    m.i.x = cos;
    m.i.z = -sin;
    m.k.x = sin;
    m.k.z = cos;
    return m;
}

// in degrees
inline mat4 rotateX(float angle)
{
    const float rad = toRadians(angle);
    const float sin = sinf(rad);
    const float cos = cosf(rad);
    mat4 m;
    m.j.y = cos;
    m.j.z = sin;
    m.k.y = -sin;
    m.k.z = cos;
    return m;
}
