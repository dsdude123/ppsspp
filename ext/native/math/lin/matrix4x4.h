#ifndef _MATH_LIN_MATRIX4X4_H
#define _MATH_LIN_MATRIX4X4_H

#include "math/lin/vec3.h"

class Quaternion;
class Matrix3x3;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define RADIANS_TO_DEGREES(rad) ((float) rad * (float) (180.0 / M_PI))
#define DEGREES_TO_RADIANS(deg) ((float) deg * (float) (M_PI / 180.0))

inline float SignOf(float x)
{
	// VR 1 if x is positive, -1 if x is negative, or 0 if x is zero
	return (float)((x > 0) - (x < 0));
}

class Matrix4x4 {
public:
	union {
		struct {
			float xx, xy, xz, xw;
			float yx, yy, yz, yw;
			float zx, zy, zz, zw;
			float wx, wy, wz, ww;
		};
		float m[16];
		float data[16];
	};

	const ::Vec3 right() const {return ::Vec3(xx, xy, xz);}
	const ::Vec3 up()		const {return ::Vec3(yx, yy, yz);}
	const ::Vec3 front() const {return ::Vec3(zx, zy, zz);}
	const ::Vec3 move()	const {return ::Vec3(wx, wy, wz);}

	void setRight(const ::Vec3 &v) {
		xx = v.x; xy = v.y; xz = v.z;
	}
	void setUp(const ::Vec3 &v) {
		yx = v.x; yy = v.y; yz = v.z;
	}
	void setFront(const ::Vec3 &v) {
		zx = v.x; zy = v.y; zz = v.z;
	}
	void setMove(const ::Vec3 &v) {
		wx = v.x; wy = v.y; wz = v.z;
	}


	Matrix4x4 &operator=(const Matrix3x3 &other);
	const float &operator[](int i) const {
		return *(((const float *)this) + i);
	}
	float &operator[](int i) {
		return *(((float *)this) + i);
	}
	Matrix4x4 operator * (const Matrix4x4 &other) const ;
	void operator *= (const Matrix4x4 &other) {
		*this = *this * other;
	}
	const float *getReadPtr() const {
		return (const float *)this;
	}
	void empty() {
		memset(this, 0, 16 * sizeof(float));
	}
	void setScaling(const float f) {
		empty();
		xx=yy=zz=f; ww=1.0f;
	}
	void setScaling(const ::Vec3 f) {
		empty();
		xx=f.x;
		yy=f.y;
		zz=f.z;
		ww=1.0f;
	}

	void setIdentity() {
		setScaling(1.0f);
	}
	void setTranslation(const ::Vec3 &trans) {
		setIdentity();
		wx = trans.x;
		wy = trans.y;
		wz = trans.z;
	}
	void setTranslationAndScaling(const ::Vec3 &trans, const ::Vec3 &scale) {
		setScaling(scale);
		wx = trans.x;
		wy = trans.y;
		wz = trans.z;
	}

	Matrix4x4 inverse() const;
	Matrix4x4 simpleInverse() const;
	Matrix4x4 transpose() const;

	void setRotationX(const float a) {
		empty();
		float c=cosf(a);
		float s=sinf(a);
		xx = 1.0f;
		yy =	c;			yz = s;
		zy = -s;			zz = c;
		ww = 1.0f;
	}
	void setRotationY(const float a)	 {
		empty();
		float c=cosf(a);
		float s=sinf(a);
		xx = c;									 xz = -s;
		yy =	1.0f;
		zx = s;									 zz = c	;
		ww = 1.0f;
	}
	void setRotationZ(const float a)	 {
		empty();
		float c=cosf(a);
		float s=sinf(a);
		xx = c;		xy = s;
		yx = -s;	 yy = c;
		zz = 1.0f; 
		ww = 1.0f;
	}
	void setRotationAxisAngle(const ::Vec3 &axis, float angle);


	void setRotation(float x,float y, float z);
	void setProjection(float near_plane, float far_plane, float fov_horiz, float aspect = 0.75f);
	void setProjectionD3D(float near_plane, float far_plane, float fov_horiz, float aspect = 0.75f);
	void setProjectionInf(float near_plane, float fov_horiz, float aspect = 0.75f);
	void setOrtho(float left, float right, float bottom, float top, float near, float far);
	void setOrthoD3D(float left, float right, float bottom, float top, float near, float far);
	void setShadow(float Lx, float Ly, float Lz, float Lw) {
		float Pa=0;
		float Pb=1;
		float Pc=0;
		float Pd=0;
		//P = normalize(Plane);
		float d = (Pa*Lx + Pb*Ly + Pc*Lz + Pd*Lw);

		xx=Pa * Lx + d;	xy=Pa * Ly;		 xz=Pa * Lz;		 xw=Pa * Lw;
		yx=Pb * Lx;			yy=Pb * Ly + d; yz=Pb * Lz;		 yw=Pb * Lw;
		zx=Pc * Lx;			zy=Pc * Ly;		 zz=Pc * Lz + d; zw=Pc * Lw;
		wx=Pd * Lx;			wy=Pd * Ly;		 wz=Pd * Lz;		 ww=Pd * Lw + d;
	}

	void setViewLookAt(const ::Vec3 &from, const ::Vec3 &at, const ::Vec3 &worldup);
	void setViewLookAtD3D(const ::Vec3 &from, const ::Vec3 &at, const ::Vec3 &worldup);
	void setViewFrame(const ::Vec3 &pos, const ::Vec3 &right, const ::Vec3 &forward, const ::Vec3 &up);
	void stabilizeOrtho() {
		/*
		front().normalize();
		right().normalize();
		up() = front() % right();
		right() = up() % front();
		*/
	}
	void toText(char *buffer, int len) const;
	bool getOpenGLProjection(float *l, float *r, float *b, float *t, float *zNear, float *zFar, float *hfov, float *vfov, bool *lefthanded) const;
	void toOpenGL(char *buffer, int len) const;
	void print() const;
	static Matrix4x4 fromPRS(const ::Vec3 &position, const Quaternion &normal, const ::Vec3 &scale);

	void translateAndScale(const ::Vec3 &trans, const ::Vec3 &scale) {
		xx = xx * scale.x + xw * trans.x;
		xy = xy * scale.y + xw * trans.y;
		xz = xz * scale.z + xw * trans.z;

		yx = yx * scale.x + yw * trans.x;
		yy = yy * scale.y + yw * trans.y;
		yz = yz * scale.z + yw * trans.z;

		zx = zx * scale.x + zw * trans.x;
		zy = zy * scale.y + zw * trans.y;
		zz = zz * scale.z + zw * trans.z;

		wx = wx * scale.x + ww * trans.x;
		wy = wy * scale.y + ww * trans.y;
		wz = wz * scale.z + ww * trans.z;
	}

	void flipAxis(int axis) {
		for (int row = 0; row < 4; ++row)
			m[4 * row + axis] = -m[4 * row + axis];
	}
public:
	static void LoadIdentity(Matrix4x4 &mtx);
	static void LoadMatrix33(Matrix4x4 &mtx, const Matrix3x3 &m33);
	static void Set(Matrix4x4 &mtx, const float mtxArray[16]);

	static void Translate(Matrix4x4 &mtx, const float vec[3]);
	static void Shear(Matrix4x4 &mtx, const float a, const float b = 0);
	static void Scale(Matrix4x4 &mtx, const float vec[3]);

	static void Multiply(const Matrix4x4 &a, const Matrix4x4 &b, Matrix4x4 &result);

};

class Matrix3x3 {
public:
	union {
		struct {
			float xx, xy, xz;
			float yx, yy, yz;
			float zx, zy, zz;
		};
		float m[9];
		float data[9];
	};

	const ::Vec3 right() const { return ::Vec3(xx, xy, xz); }
	const ::Vec3 up()		const { return ::Vec3(yx, yy, yz); }
	const ::Vec3 front() const { return ::Vec3(zx, zy, zz); }

	void setRight(const ::Vec3 &v) {
		xx = v.x; xy = v.y; xz = v.z;
	}
	void setUp(const ::Vec3 &v) {
		yx = v.x; yy = v.y; yz = v.z;
	}
	void setFront(const ::Vec3 &v) {
		zx = v.x; zy = v.y; zz = v.z;
	}

	const float &operator[](int i) const {
		return *(((const float *)this) + i);
	}
	float &operator[](int i) {
		return *(((float *)this) + i);
	}
	Matrix3x3 &operator=(const Matrix4x4 &other) {
		xx = other.xx; xy = other.xy; xz = other.xz;
		yx = other.yx; yy = other.yy; yz = other.yz;
		zx = other.zx; zy = other.zy; zz = other.zz;
		return *this;
	}
	Matrix3x3 operator * (const Matrix3x3 &other) const;
	void operator *= (const Matrix3x3 &other) {
		*this = *this * other;
	}
	const float *getReadPtr() const {
		return (const float *)this;
	}
	void empty() {
		memset(this, 0, 9 * sizeof(float));
	}
	void setScaling(const float f) {
		empty();
		xx = yy = zz = f;
	}
	void setScaling(const ::Vec3 f) {
		empty();
		xx = f.x;
		yy = f.y;
		zz = f.z;
	}

	void setIdentity() {
		setScaling(1.0f);
	}

	Matrix3x3 inverse() const;
	Matrix3x3 simpleInverse() const;
	Matrix3x3 transpose() const;

	void setRotationX(const float a) {
		empty();
		float c = cosf(a);
		float s = sinf(a);
		xx = 1.0f;
		yy = c;			yz = s;
		zy = -s;			zz = c;
	}
	void setRotationY(const float a)	 {
		empty();
		float c = cosf(a);
		float s = sinf(a);
		xx = c;									 xz = -s;
		yy = 1.0f;
		zx = s;									 zz = c;
	}
	void setRotationZ(const float a)	 {
		empty();
		float c = cosf(a);
		float s = sinf(a);
		xx = c;		xy = s;
		yx = -s;	 yy = c;
		zz = 1.0f;
	}
	void setRotationAxisAngle(const ::Vec3 &axis, float angle);
	void setRotation(float x, float y, float z);

	void toText(char *buffer, int len) const;
	void print() const;
	static Matrix3x3 fromPRS(const ::Vec3 &position, const Quaternion &normal, const ::Vec3 &scale);

	void flipAxis(int axis) {
		for (int row = 0; row < 3; ++row)
			m[3 * row + axis] = -m[3 * row + axis];
	}
public:
	static void LoadIdentity(Matrix3x3 &mtx);
	static void LoadQuaternion(Matrix3x3 &mtx, const Quaternion &quat);

	// set mtx to be a rotation matrix around the x axis
	static void RotateX(Matrix3x3 &mtx, float rad);
	// set mtx to be a rotation matrix around the y axis
	static void RotateY(Matrix3x3 &mtx, float rad);
	// set mtx to be a rotation matrix around the z axis
	static void RotateZ(Matrix3x3 &mtx, float rad);

	// set result = a x b
	static void Multiply(const Matrix3x3 &a, const Matrix3x3 &b, Matrix3x3 &result);
	static void Multiply(const Matrix3x3 &a, const float vec[3], float result[3]);

	static void GetPieYawPitchRollR(const Matrix3x3 &m, float &yaw, float &pitch, float &roll);
};


#endif	// _MATH_LIN_MATRIX4X4_H

