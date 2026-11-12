// Minimal vector/matrix maths. Matrices are column-major (GL layout).
// World space follows Unity: left-handed, +Y up, +Z forward.
#pragma once
#include <math.h>

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return sqrtf(dot(a, a)); }
inline Vec3 normalize(Vec3 a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }
inline Vec3 vmin(Vec3 a, Vec3 b) { return {fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)}; }
inline Vec3 vmax(Vec3 a, Vec3 b) { return {fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)}; }

struct Quat {
    float x, y, z, w;
    Quat() : x(0), y(0), z(0), w(1) {}
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};
inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Vec3 rotate(Quat q, Vec3 v) {
    Vec3 u(q.x, q.y, q.z);
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
// Unity's Quaternion.Euler: degrees, applied Z, then X, then Y.
inline Quat eulerDeg(float pitch, float yaw, float roll) {
    const float d = 3.14159265f / 360.0f;
    Quat qx(sinf(pitch * d), 0, 0, cosf(pitch * d));
    Quat qy(0, sinf(yaw * d), 0, cosf(yaw * d));
    Quat qz(0, 0, sinf(roll * d), cosf(roll * d));
    return qy * qx * qz;
}

struct Mat4 {
    float m[16];
    static Mat4 identity() { Mat4 r = {}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
    float &at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    Vec3 point(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3 dir(Vec3 p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z,
                m[1] * p.x + m[5] * p.y + m[9] * p.z,
                m[2] * p.x + m[6] * p.y + m[10] * p.z};
    }
    float det3() const {
        return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
               m[8] * (m[1] * m[6] - m[5] * m[2]);
    }
};
inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
inline Mat4 trs(Vec3 t, Quat q, Vec3 s) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 r = Mat4::identity();
    r.m[0] = (1 - 2 * (yy + zz)) * s.x; r.m[1] = 2 * (xy + wz) * s.x;       r.m[2] = 2 * (xz - wy) * s.x;
    r.m[4] = 2 * (xy - wz) * s.y;       r.m[5] = (1 - 2 * (xx + zz)) * s.y; r.m[6] = 2 * (yz + wx) * s.y;
    r.m[8] = 2 * (xz + wy) * s.z;       r.m[9] = 2 * (yz - wx) * s.z;       r.m[10] = (1 - 2 * (xx + yy)) * s.z;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
// View matrix for a camera at `pos` looking along rotate(q, +Z), still in
// Unity's left-handed space (projection does the handedness flip).
inline Mat4 viewFrom(Vec3 pos, Quat q) {
    Quat inv(-q.x, -q.y, -q.z, q.w);
    Mat4 r = trs(Vec3(), inv, Vec3(1, 1, 1));
    Vec3 t = rotate(inv, pos) * -1.0f;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
// GL perspective with a Z mirror, so left-handed view space (+Z forward) maps
// to GL clip space. The mirror also turns Unity's clockwise front faces into
// GL's default counter-clockwise ones.
inline Mat4 perspectiveLH(float fovYDeg, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovYDeg * 3.14159265f / 360.0f);
    Mat4 r = {};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zf - zn);
    r.m[11] = 1;
    r.m[14] = -2 * zf * zn / (zf - zn);
    return r;
}

// Frustum planes from a view-projection matrix, for sphere culling.
struct Frustum {
    float p[6][4];
    void build(const Mat4 &vp) {
        for (int i = 0; i < 3; i++)
            for (int s = 0; s < 2; s++) {
                float *pl = p[i * 2 + s];
                float sign = s ? -1.0f : 1.0f;
                for (int c = 0; c < 4; c++) pl[c] = vp.at(3, c) + sign * vp.at(i, c);
                float l = sqrtf(pl[0] * pl[0] + pl[1] * pl[1] + pl[2] * pl[2]);
                for (int c = 0; c < 4; c++) pl[c] /= l;
            }
    }
    bool sphere(Vec3 c, float r) const {
        for (int i = 0; i < 6; i++)
            if (p[i][0] * c.x + p[i][1] * c.y + p[i][2] * c.z + p[i][3] < -r) return false;
        return true;
    }
};

// Unity's Quaternion.eulerAngles.y (Z-X-Y order), in degrees, including the
// gimbal-lock case (pitch at +-90) where Unity sets z = 0 and puts the rest in y.
// SEUM spawn rotations carry a 90 degree tilt from the modelling tool, so this
// case is the common one.
inline float unityYawDeg(Quat q) {
    float sinp = 2 * (q.w * q.x - q.y * q.z);
    if (fabsf(sinp) > 0.9995f) {
        float h = (sinp > 0 ? 1 : -1) * 3.14159265f / 4;   // half of +-90 degrees
        Quat qxInv(-sinf(h), 0, 0, cosf(h));
        Quat qy = q * qxInv;
        return 2 * atan2f(qy.y, qy.w) * 57.2957795f;
    }
    return atan2f(2 * (q.w * q.y + q.x * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)) * 57.2957795f;
}
