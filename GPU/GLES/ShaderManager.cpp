// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

// #define SHADERLOG

#if defined(_WIN32) && defined(SHADERLOG)
#include "Common/CommonWindows.h"
#endif

#include <map>
#include <cstdio>

#include "base/logging.h"
#include "math/math_util.h"
#include "math/lin/matrix4x4.h"
#include "profiler/profiler.h"

#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/VR.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/GLStateCache.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/TransformPipeline.h"
#include "UI/OnScreenDisplay.h"
#include "Framebuffer.h"
#include "i18n/i18n.h"

#define HACK_LOG NOTICE_LOG

FramebufferManager* _framebufferManager = nullptr;

//VR Virtual Reality debugging variables
static float s_locked_skybox[3 * 4];
static bool s_had_skybox = false;
bool m_layer_on_top;
static bool bViewportChanged = true, bFreeLookChanged = true, bFrameChanged = true;
int vr_render_eye = -1;
int debug_viewportNum = 0;
//Viewport debug_vpList[64] = { 0 };
int debug_projNum = 0;
float debug_projList[64][7] = { 0 };
int vr_widest_3d_projNum = -1;
bool this_frame_has_3d = false, last_frame_had_3d = false;
//EFBRectangle g_final_screen_region = EFBRectangle(0, 0, 640, 528);
//EFBRectangle g_requested_viewport = EFBRectangle(0, 0, 640, 528), g_rendered_viewport = EFBRectangle(0, 0, 640, 528);
enum ViewportType g_viewport_type = VIEW_FULLSCREEN, g_old_viewport_type = VIEW_FULLSCREEN;
enum SplitScreenType {
	SS_FULLSCREEN = 0,
	SS_2_PLAYER_SIDE_BY_SIDE,
	SS_2_PLAYER_OVER_UNDER,
	SS_QUADRANTS,
	SS_3_PLAYER_TOP,
	SS_3_PLAYER_LEFT,
	SS_3_PLAYER_RIGHT,
	SS_3_PLAYER_BOTTOM,
	SS_3_PLAYER_COLUMNS,
	SS_CUSTOM
};
enum SplitScreenType g_splitscreen_type = SS_FULLSCREEN, g_old_splitscreen_type = SS_FULLSCREEN;
bool g_is_skybox = false, g_is_skyplane = false;

static float oldpos[3] = { 0, 0, 0 }, totalpos[3] = { 0, 0, 0 };

const char *GetViewportTypeName(ViewportType v)
{
	if (g_is_skybox)
		return "Skybox";
	switch (v)
	{
	case VIEW_FULLSCREEN:
		return "Fullscreen";
	case VIEW_LETTERBOXED:
		return "Letterboxed";
	case VIEW_HUD_ELEMENT:
		return "HUD element";
	case VIEW_OFFSCREEN:
		return "Offscreen";
	case VIEW_RENDER_TO_TEXTURE:
		return "Render to Texture";
	case VIEW_PLAYER_1:
		return "Player 1";
	case VIEW_PLAYER_2:
		return "Player 2";
	case VIEW_PLAYER_3:
		return "Player 3";
	case VIEW_PLAYER_4:
		return "Player 4";
	case VIEW_SKYBOX:
		return "Skybox";
	default:
		return "Error";
	}
}

void ClearDebugProj() { //VR
	bFrameChanged = true;

	debug_newScene = debug_nextScene;
	if (debug_newScene)
	{
		//HACK_LOG(VR, "***** New scene *****");
		// General VR hacks
		vr_widest_3d_projNum = -1;
		//ELOG("********* New scene *********");
	}
	// Only change from a HUD back to a 2D screen if we have had no 3D for 2 frames, this prevents the alternating 2D/HUD flashing bug.
	if (!this_frame_has_3d && !last_frame_had_3d) {
		//ELOG("**** Resetting to 2D ****");
		vr_widest_3d_HFOV = 0;
		vr_widest_3d_VFOV = 0;
		vr_widest_3d_zNear = 0;
		vr_widest_3d_zFar = 0;
	} else if (this_frame_widest_HFOV > 0) {
		vr_widest_3d_HFOV = this_frame_widest_HFOV;
		vr_widest_3d_VFOV = this_frame_widest_VFOV;
		vr_widest_3d_zNear = this_frame_widest_zNear;
		vr_widest_3d_zFar = this_frame_widest_zFar;
		//ELOG("**** this frame: H=%g, V=%g, n=%g, f=%g ****", vr_widest_3d_HFOV, vr_widest_3d_VFOV, vr_widest_3d_zNear, vr_widest_3d_zFar);
	} else {
		//ELOG("**** remembered: H=%g, V=%g, n=%g, f=%g ****", vr_widest_3d_HFOV, vr_widest_3d_VFOV, vr_widest_3d_zNear, vr_widest_3d_zFar);
	}
	debug_nextScene = false;
	debug_projNum = 0;
	debug_viewportNum = 0;
	last_frame_had_3d = this_frame_has_3d;
	this_frame_has_3d = false;
	this_frame_widest_HFOV = 0;
	this_frame_widest_VFOV = 0;
	this_frame_widest_zNear = 0;
	this_frame_widest_zFar = 0;

	// Metroid Prime hacks
	//NewMetroidFrame();
}

void DoLogProj(int j, float p[], const char *s) { //VR
	//if (j == g_Config.iSelectedLayer)
	//	HACK_LOG(VR, "** SELECTED LAYER:");
	if (p[6] != -1) { // orthographic projection
		//float right = p[0]-(p[0]*p[1]);
		//float left = right - 2/p[0];

		float left = -(p[1] + 1) / p[0];
		float right = left + 2 / p[0];
		float bottom = -(p[3] + 1) / p[2];
		float top = bottom + 2 / p[2];
		float zfar = p[5] / p[4];
		float znear = (1 + p[4] * zfar) / p[4];
		//HACK_LOG(VR, "%d: 2D: %s (%g, %g) to (%g, %g); z: %g to %g  [%g, %g]", j, s, left, top, right, bottom, znear, zfar, p[4], p[5]);
	}
	else if (p[0] != 0 || p[2] != 0) { // perspective projection
		float f = p[5] / p[4];
		float n = f*p[4] / (p[4] - 1);
		if (p[1] != 0.0f || p[3] != 0.0f) {
			//HACK_LOG(VR, "%d: %s OFF-AXIS Perspective: 2n/w=%.2f A=%.2f; 2n/h=%.2f B=%.2f; n=%.2f f=%.2f", j, s, p[0], p[1], p[2], p[3], p[4], p[5]);
			//HACK_LOG(VR, "	HFOV: %.2f    VFOV: %.2f   Aspect Ratio: 16:%.1f", 2 * atan(1.0f / p[0])*180.0f / 3.1415926535f, 2 * atan(1.0f / p[2])*180.0f / 3.1415926535f, 16 / (2 / p[0])*(2 / p[2]));
		}
		else {
			//HACK_LOG(VR, "%d: %s HFOV: %.2fdeg; VFOV: %.2fdeg; Aspect Ratio: 16:%.1f; near:%f, far:%f", j, s, 2 * atan(1.0f / p[0])*180.0f / 3.1415926535f, 2 * atan(1.0f / p[2])*180.0f / 3.1415926535f, 16 / (2 / p[0])*(2 / p[2]), n, f);
		}
	}
	else { // invalid
		//HACK_LOG(VR, "%d: %s ZERO", j, s);
	}
}

void LogProj(const Matrix4x4 & m) { //VR
	float p[7];
	p[0] = m.xx;
	p[2] = m.yy;
	p[4] = m.zz;
	p[5] = m.wz;
	p[6] = m.zw;
	float left, right, bottom, top, zfar, znear, vfov, hfov;
	bool lefthanded;
	m.getOpenGLProjection(&left, &right, &bottom, &top, &znear, &zfar, &hfov, &vfov, &lefthanded);
	vfov = vfov*180.0f / 3.1415926535f;
	hfov = hfov*180.0f / 3.1415926535f;

	if (m.zw == -1.0f || m.zw == 1.0f) { // perspective projection
		p[1] = m.zx;
		p[3] = m.zy;

		float h = fabs(hfov);

		if (h > this_frame_widest_HFOV && h <= 125 && (fabs(m.yy) != fabs(m.xx))) {
			this_frame_widest_HFOV = h;
			this_frame_widest_VFOV = fabs(vfov);
			this_frame_widest_zNear = fabs(znear);
			this_frame_widest_zFar = fabs(zfar);
			if (h > vr_widest_3d_HFOV) {
				vr_widest_3d_projNum = debug_projNum;
				vr_widest_3d_HFOV = h;
				vr_widest_3d_VFOV = fabs(vfov);
				vr_widest_3d_zNear = fabs(znear);
				vr_widest_3d_zFar = fabs(zfar);
				//ELOG("* widening: H=%g, V=%g, n=%g, f=%g *", vr_widest_3d_HFOV, vr_widest_3d_VFOV, vr_widest_3d_zNear, vr_widest_3d_zFar);
			}
		}

		if (debug_newScene && h > vr_widest_3d_HFOV && h <= 125 && (fabs(m.yy) != fabs(m.xx))) {
			//DEBUG_LOG(VR, "***** New Widest 3D *****");

			vr_widest_3d_projNum = debug_projNum;
			vr_widest_3d_HFOV = h;
			vr_widest_3d_VFOV = fabs(vfov);
			vr_widest_3d_zNear = fabs(znear);
			vr_widest_3d_zFar = fabs(zfar);
			//DEBUG_LOG(VR, "%d: %g x %g deg, n=%g f=%g, p4=%g p5=%g; xs=%g ys=%g", vr_widest_3d_projNum, vr_widest_3d_HFOV, vr_widest_3d_VFOV, vr_widest_3d_zNear, vr_widest_3d_zFar, m.zz, m.wz, m.xx, m.yy);
			//ELOG("** widening: H=%g, V=%g, n=%g, f=%g **", vr_widest_3d_HFOV, vr_widest_3d_VFOV, vr_widest_3d_zNear, vr_widest_3d_zFar);
		}
	}
	else
	{
		p[1] = m.wx;
		p[3] = m.wy;
		float left = -(m.wx + 1) / m.xx;
		float right = left + 2 / m.xx;
		float bottom = -(m.wy + 1) / m.yy;
		float top = bottom + 2 / m.yy;
		float zfar = m.wz / m.zz;
		float znear = (1 + m.zz * zfar) / m.zz;
	}

	if (debug_projNum >= 64)
		return;
	if (!debug_newScene) {
		for (int i = 0; i<7; i++) {
			if (debug_projList[debug_projNum][i] != p[i]) {
				debug_nextScene = true;
				debug_projList[debug_projNum][i] = p[i];
			}
		}
		// wait until next frame
		//if (debug_newScene) {
		//	INFO_LOG(VIDEO,"***** New scene *****");
		//	for (int j=0; j<debug_projNum; j++) {
		//		DoLogProj(j, debug_projList[j]);
		//	}
		//}
	}
	else
	{
		debug_nextScene = false;
		//INFO_LOG(VR, "%f Units Per Metre", g_Config.fUnitsPerMetre);
		//INFO_LOG(VR, "HUD is %.1fm away and %.1fm thick", g_Config.fHudDistance, g_Config.fHudThickness);
		DoLogProj(debug_projNum, debug_projList[debug_projNum], "unknown");
	}
	debug_projNum++;
}

Shader::Shader(const char *code, uint32_t glShaderType, bool useHWTransform, const ShaderID &shaderID)
	  : id_(shaderID), failed_(false), useHWTransform_(useHWTransform) {
	PROFILE_THIS_SCOPE("shadercomp");
	isFragment_ = glShaderType == GL_FRAGMENT_SHADER;
	source_ = code;
#ifdef SHADERLOG
#ifdef _WIN32
	OutputDebugStringUTF8(code);
#else
	printf("%s\n", code);
#endif
#endif
	shader = glCreateShader(glShaderType);
	glShaderSource(shader, 1, &code, 0);
	glCompileShader(shader);
	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
#define MAX_INFO_LOG_SIZE 2048
		GLchar infoLog[MAX_INFO_LOG_SIZE];
		GLsizei len;
		glGetShaderInfoLog(shader, MAX_INFO_LOG_SIZE, &len, infoLog);
		infoLog[len] = '\0';
#ifdef ANDROID
		ELOG("Error in shader compilation! %s\n", infoLog);
		ELOG("Shader source:\n%s\n", (const char *)code);
#endif
		ERROR_LOG(G3D, "Error in shader compilation!\n");
		ERROR_LOG(G3D, "Info log: %s\n", infoLog);
		ERROR_LOG(G3D, "Shader source:\n%s\n", (const char *)code);
		Reporting::ReportMessage("Error in shader compilation: info: %s / code: %s", infoLog, (const char *)code);
#ifdef SHADERLOG
		OutputDebugStringUTF8(infoLog);
#endif
		failed_ = true;
		shader = 0;
	} else {
		DEBUG_LOG(G3D, "Compiled shader:\n%s\n", (const char *)code);
	}
}

Shader::~Shader() {
	if (shader)
		glDeleteShader(shader);
}

LinkedShader::LinkedShader(Shader *vs, Shader *gs, Shader *fs, u32 vertType, bool useHWTransform, LinkedShader *previous, bool isClear)
		: useHWTransform_(useHWTransform), program(0), dirtyUniforms(0) {
	PROFILE_THIS_SCOPE("shaderlink");

	program = glCreateProgram();
	vs_ = vs;
	gs_ = gs;
	glAttachShader(program, vs->shader);
	glAttachShader(program, gs->shader);
	glAttachShader(program, fs->shader);

	// Bind attribute locations to fixed locations so that they're
	// the same in all shaders. We use this later to minimize the calls to
	// glEnableVertexAttribArray and glDisableVertexAttribArray.
	glBindAttribLocation(program, ATTR_POSITION, "position");
	glBindAttribLocation(program, ATTR_TEXCOORD, "texcoord");
	glBindAttribLocation(program, ATTR_NORMAL, "normal");
	glBindAttribLocation(program, ATTR_W1, "w1");
	glBindAttribLocation(program, ATTR_W2, "w2");
	glBindAttribLocation(program, ATTR_COLOR0, "color0");
	glBindAttribLocation(program, ATTR_COLOR1, "color1");

#if !defined(USING_GLES2)
	if (gstate_c.featureFlags & GPU_SUPPORTS_DUALSOURCE_BLEND) {
		// Dual source alpha
		glBindFragDataLocationIndexed(program, 0, 0, "fragColor0");
		glBindFragDataLocationIndexed(program, 0, 1, "fragColor1");
	} else if (gl_extensions.VersionGEThan(3, 3, 0)) {
		glBindFragDataLocation(program, 0, "fragColor0");
	}
#elif !defined(IOS)
	if (gl_extensions.GLES3) {
		if (gstate_c.featureFlags & GPU_SUPPORTS_DUALSOURCE_BLEND) {
			glBindFragDataLocationIndexedEXT(program, 0, 0, "fragColor0");
			glBindFragDataLocationIndexedEXT(program, 0, 1, "fragColor1");
		}
	}
#endif

	glLinkProgram(program);

	GLint linkStatus = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE) {
		GLint bufLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char* buf = new char[bufLength];
			glGetProgramInfoLog(program, bufLength, NULL, buf);
#ifdef ANDROID
			ELOG("Could not link program:\n %s", buf);
#endif
			ERROR_LOG(G3D, "Could not link program:\n %s", buf);
			ERROR_LOG(G3D, "VS desc:\n%s\n", vs->GetShaderString(SHADER_STRING_SHORT_DESC).c_str());
			ERROR_LOG(G3D, "GS desc:\n%s\n", gs->GetShaderString(SHADER_STRING_SHORT_DESC).c_str());
			ERROR_LOG(G3D, "FS desc:\n%s\n", fs->GetShaderString(SHADER_STRING_SHORT_DESC).c_str());
			std::string vs_source = vs->GetShaderString(SHADER_STRING_SOURCE_CODE);
			std::string gs_source = gs->GetShaderString(SHADER_STRING_SOURCE_CODE);
			std::string fs_source = fs->GetShaderString(SHADER_STRING_SOURCE_CODE);
			ERROR_LOG(G3D, "VS:\n%s\n", vs_source.c_str());
			ERROR_LOG(G3D, "GS:\n%s\n", gs_source.c_str());
			ERROR_LOG(G3D, "FS:\n%s\n", fs_source.c_str());
			Reporting::ReportMessage("Error in shader program link: info: %s / fs: %s / gs: %s / vs: %s", buf, fs_source.c_str(), gs_source.c_str(), vs_source.c_str());
#ifdef SHADERLOG
			OutputDebugStringUTF8(buf);
			OutputDebugStringUTF8(vs_source.c_str());
			OutputDebugStringUTF8(gs_source.c_str());
			OutputDebugStringUTF8(fs_source.c_str());
#endif
			delete [] buf;	// we're dead!
		}
		// Prevent a buffer overflow.
		numBones = 0;
		return;
	}

	INFO_LOG(G3D, "Linked shader: vs %i gs %i fs %i", (int)vs->shader, (int)gs->shader, (int)fs->shader);

	u_tex = glGetUniformLocation(program, "tex");
	u_proj = glGetUniformLocation(program, "u_proj");
	u_proj_through = glGetUniformLocation(program, "u_proj_through");
	u_StereoParams = glGetUniformLocation(program, "u_StereoParams");
	u_texenv = glGetUniformLocation(program, "u_texenv");
	u_fogcolor = glGetUniformLocation(program, "u_fogcolor");
	u_fogcoef = glGetUniformLocation(program, "u_fogcoef");
	u_alphacolorref = glGetUniformLocation(program, "u_alphacolorref");
	u_alphacolormask = glGetUniformLocation(program, "u_alphacolormask");
	u_stencilReplaceValue = glGetUniformLocation(program, "u_stencilReplaceValue");
	u_testtex = glGetUniformLocation(program, "testtex");

	u_fbotex = glGetUniformLocation(program, "fbotex");
	u_blendFixA = glGetUniformLocation(program, "u_blendFixA");
	u_blendFixB = glGetUniformLocation(program, "u_blendFixB");
	u_fbotexSize = glGetUniformLocation(program, "u_fbotexSize");

	// Transform
	u_view = glGetUniformLocation(program, "u_view");
	u_world = glGetUniformLocation(program, "u_world");
	u_texmtx = glGetUniformLocation(program, "u_texmtx");
	if (vertTypeGetWeightMask(vertType) != GE_VTYPE_WEIGHT_NONE)
		numBones = TranslateNumBones(vertTypeGetNumBoneWeights(vertType));
	else
		numBones = 0;
	u_depthRange = glGetUniformLocation(program, "u_depthRange");

#ifdef USE_BONE_ARRAY
	u_bone = glGetUniformLocation(program, "u_bone");
#else
	for (int i = 0; i < 8; i++) {
		char name[10];
		sprintf(name, "u_bone%i", i);
		u_bone[i] = glGetUniformLocation(program, name);
	}
#endif

	// Lighting, texturing
	u_ambient = glGetUniformLocation(program, "u_ambient");
	u_matambientalpha = glGetUniformLocation(program, "u_matambientalpha");
	u_matdiffuse = glGetUniformLocation(program, "u_matdiffuse");
	u_matspecular = glGetUniformLocation(program, "u_matspecular");
	u_matemissive = glGetUniformLocation(program, "u_matemissive");
	u_uvscaleoffset = glGetUniformLocation(program, "u_uvscaleoffset");
	u_texclamp = glGetUniformLocation(program, "u_texclamp");
	u_texclampoff = glGetUniformLocation(program, "u_texclampoff");

	for (int i = 0; i < 4; i++) {
		char temp[64];
		sprintf(temp, "u_lightpos%i", i);
		u_lightpos[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightdir%i", i);
		u_lightdir[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightatt%i", i);
		u_lightatt[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightangle%i", i);
		u_lightangle[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightspotCoef%i", i);
		u_lightspotCoef[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightambient%i", i);
		u_lightambient[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightdiffuse%i", i);
		u_lightdiffuse[i] = glGetUniformLocation(program, temp);
		sprintf(temp, "u_lightspecular%i", i);
		u_lightspecular[i] = glGetUniformLocation(program, temp);
	}

	attrMask = 0;
	if (-1 != glGetAttribLocation(program, "position")) attrMask |= 1 << ATTR_POSITION;
	if (-1 != glGetAttribLocation(program, "texcoord")) attrMask |= 1 << ATTR_TEXCOORD;
	if (-1 != glGetAttribLocation(program, "normal")) attrMask |= 1 << ATTR_NORMAL;
	if (-1 != glGetAttribLocation(program, "w1")) attrMask |= 1 << ATTR_W1;
	if (-1 != glGetAttribLocation(program, "w2")) attrMask |= 1 << ATTR_W2;
	if (-1 != glGetAttribLocation(program, "color0")) attrMask |= 1 << ATTR_COLOR0;
	if (-1 != glGetAttribLocation(program, "color1")) attrMask |= 1 << ATTR_COLOR1;

	availableUniforms = 0;
	if (u_proj != -1) availableUniforms |= DIRTY_PROJMATRIX;
	if (u_proj_through != -1) availableUniforms |= DIRTY_PROJTHROUGHMATRIX;
	if (u_texenv != -1) availableUniforms |= DIRTY_TEXENV;
	if (u_alphacolorref != -1) availableUniforms |= DIRTY_ALPHACOLORREF;
	if (u_alphacolormask != -1) availableUniforms |= DIRTY_ALPHACOLORMASK;
	if (u_fogcolor != -1) availableUniforms |= DIRTY_FOGCOLOR;
	if (u_fogcoef != -1) availableUniforms |= DIRTY_FOGCOEF;
	if (u_texenv != -1) availableUniforms |= DIRTY_TEXENV;
	if (u_uvscaleoffset != -1) availableUniforms |= DIRTY_UVSCALEOFFSET;
	if (u_texclamp != -1) availableUniforms |= DIRTY_TEXCLAMP;
	if (u_world != -1) availableUniforms |= DIRTY_WORLDMATRIX;
	if (u_view != -1) availableUniforms |= DIRTY_VIEWMATRIX;
	if (u_texmtx != -1) availableUniforms |= DIRTY_TEXMATRIX;
	if (u_stencilReplaceValue != -1) availableUniforms |= DIRTY_STENCILREPLACEVALUE;
	if (u_blendFixA != -1 || u_blendFixB != -1 || u_fbotexSize != -1) availableUniforms |= DIRTY_SHADERBLEND;
	if (u_depthRange != -1)
		availableUniforms |= DIRTY_DEPTHRANGE;

	// Looping up to numBones lets us avoid checking u_bone[i]
#ifdef USE_BONE_ARRAY
	if (u_bone != -1) {
		for (int i = 0; i < numBones; i++) {
			availableUniforms |= DIRTY_BONEMATRIX0 << i;
		}
	}
#else
	for (int i = 0; i < numBones; i++) {
		if (u_bone[i] != -1)
			availableUniforms |= DIRTY_BONEMATRIX0 << i;
	}
#endif
	if (u_ambient != -1) availableUniforms |= DIRTY_AMBIENT;
	if (u_matambientalpha != -1) availableUniforms |= DIRTY_MATAMBIENTALPHA;
	if (u_matdiffuse != -1) availableUniforms |= DIRTY_MATDIFFUSE;
	if (u_matemissive != -1) availableUniforms |= DIRTY_MATEMISSIVE;
	if (u_matspecular != -1) availableUniforms |= DIRTY_MATSPECULAR;
	for (int i = 0; i < 4; i++) {
		if (u_lightdir[i] != -1 ||
				u_lightspecular[i] != -1 ||
				u_lightpos[i] != -1)
			availableUniforms |= DIRTY_LIGHT0 << i;
	}

	glUseProgram(program);

	// Default uniform values
	glUniform1i(u_tex, 0);
	glUniform1i(u_fbotex, 1);
	glUniform1i(u_testtex, 2);
	// The rest, use the "dirty" mechanism.
	dirtyUniforms = DIRTY_ALL;
	bFrameChanged = true;
	use(vertType, previous, isClear);
}

LinkedShader::~LinkedShader() {
	// Shaders are automatically detached by glDeleteProgram.
	glDeleteProgram(program);
}

// Utility
static void SetColorUniform3(int uniform, u32 color) {
	const float col[3] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f
	};
	glUniform3fv(uniform, 1, col);
}

static void SetColorUniform3Alpha(int uniform, u32 color, u8 alpha) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		alpha/255.0f
	};
	glUniform4fv(uniform, 1, col);
}

// This passes colors unscaled (e.g. 0 - 255 not 0 - 1.)
static void SetColorUniform3Alpha255(int uniform, u32 color, u8 alpha) {
	if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
		const float col[4] = {
			(float)((color & 0xFF) >> 0) * (1.0f / 255.0f),
			(float)((color & 0xFF00) >> 8) * (1.0f / 255.0f),
			(float)((color & 0xFF0000) >> 16) * (1.0f / 255.0f),
			(float)alpha * (1.0f / 255.0f)
		};
		glUniform4fv(uniform, 1, col);
	} else {
		const float col[4] = {
			(float)((color & 0xFF) >> 0),
			(float)((color & 0xFF00) >> 8),
			(float)((color & 0xFF0000) >> 16),
			(float)alpha 
		};
		glUniform4fv(uniform, 1, col);
	}
}

static void SetColorUniform3iAlpha(int uniform, u32 color, u8 alpha) {
	const int col[4] = {
		(int)((color & 0xFF) >> 0),
		(int)((color & 0xFF00) >> 8),
		(int)((color & 0xFF0000) >> 16),
		(int)alpha,
	};
	glUniform4iv(uniform, 1, col);
}

static void SetColorUniform3ExtraFloat(int uniform, u32 color, float extra) {
	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		extra
	};
	glUniform4fv(uniform, 1, col);
}

static void SetFloat24Uniform3(int uniform, const u32 data[3]) {
	const u32 col[3] = {
		data[0] << 8, data[1] << 8, data[2] << 8
	};
	glUniform3fv(uniform, 1, (const GLfloat *)&col[0]);
}

static void SetFloatUniform4(int uniform, float data[4]) {
	glUniform4fv(uniform, 1, data);
}

static void SetMatrix4x3(int uniform, const float *m4x3) {
	float m4x4[16];
	ConvertMatrix4x3To4x4(m4x4, m4x3);
	glUniformMatrix4fv(uniform, 1, GL_FALSE, m4x4);
}

static inline void ScaleProjMatrix(Matrix4x4 &in) {
	float yOffset = gstate_c.vpYOffset;
	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE) {
		// GL upside down is a pain as usual.
		yOffset = -yOffset;
	}
	const Vec3 trans(gstate_c.vpXOffset, yOffset, gstate_c.vpZOffset * 2.0f);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale);
	in.translateAndScale(trans, scale);
}

u32 LinkedShader::use(u32 vertType, LinkedShader *previous, bool isClear) {
	glUseProgram(program);
	u32 stillDirty = UpdateUniforms(vertType, isClear);
	int enable, disable;
	if (previous) {
		enable = attrMask & ~previous->attrMask;
		disable = (~attrMask) & previous->attrMask;
	} else {
		enable = attrMask;
		disable = ~attrMask;
	}
	for (int i = 0; i < ATTR_COUNT; i++) {
		if (enable & (1 << i))
			glEnableVertexAttribArray(i);
		else if (disable & (1 << i))
			glDisableVertexAttribArray(i);
	}
	return stillDirty;
}

void LinkedShader::stop() {
	for (int i = 0; i < ATTR_COUNT; i++) {
		if (attrMask & (1 << i))
			glDisableVertexAttribArray(i);
	}
}

u32 LinkedShader::UpdateUniforms(u32 vertType, bool isClear) {
	//if (isClear)
	//	ELOG("Clear");

	u32 dirty = dirtyUniforms & availableUniforms;
	dirtyUniforms &= (DIRTY_PROJMATRIX | DIRTY_PROJTHROUGHMATRIX);
	if (!dirty)
		return dirtyUniforms;

	static bool temp_skybox = false;
	bool position_changed = false, skybox_changed = false;

	// Update any dirty uniforms before we draw
	if (dirty & DIRTY_TEXENV) {
		SetColorUniform3(u_texenv, gstate.texenvcolor);
	}
	if (dirty & DIRTY_ALPHACOLORREF) {
		SetColorUniform3Alpha255(u_alphacolorref, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
	}
	if (dirty & DIRTY_ALPHACOLORMASK) {
		SetColorUniform3iAlpha(u_alphacolormask, gstate.colortestmask, gstate.getAlphaTestMask());
	}
	if (dirty & DIRTY_FOGCOLOR) {
		SetColorUniform3(u_fogcolor, gstate.fogcolor);
	}
	if (dirty & DIRTY_FOGCOEF) {
		float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		if (my_isinf(fogcoef[1])) {
			// not really sure what a sensible value might be.
			fogcoef[1] = fogcoef[1] < 0.0f ? -10000.0f : 10000.0f;
		} else if (my_isnan(fogcoef[1])) {
			// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
			// Just put the fog far away at a large finite distance.
			// Infinities and NaNs are rather unpredictable in shaders on many GPUs
			// so it's best to just make it a sane calculation.
			fogcoef[0] = 100000.0f;
			fogcoef[1] = 1.0f;
		}
#ifndef MOBILE_DEVICE
		else if (my_isnanorinf(fogcoef[1]) || my_isnanorinf(fogcoef[0])) {
			ERROR_LOG_REPORT_ONCE(fognan, G3D, "Unhandled fog NaN/INF combo: %f %f", fogcoef[0], fogcoef[1]);
		}
#endif
		glUniform2fv(u_fogcoef, 1, fogcoef);
	}

	// Texturing

	// If this dirty check is changed to true, Frontier Gate Boost works in texcoord speedhack mode.
	// This means that it's not a flushing issue.
	// It uses GE_TEXMAP_TEXTURE_MATRIX with GE_PROJMAP_UV a lot.
	// Can't figure out why it doesn't dirty at the right points though...
	if (dirty & DIRTY_UVSCALEOFFSET) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		static const float rescale[4] = {1.0f, 2*127.5f/128.f, 2*32767.5f/32768.f, 1.0f};
		const float factor = rescale[(vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT];

		float uvscaleoff[4];

		switch (gstate.getUVGenMode()) {
		case GE_TEXMAP_TEXTURE_COORDS:
			// Not sure what GE_TEXMAP_UNKNOWN is, but seen in Riviera.  Treating the same as GE_TEXMAP_TEXTURE_COORDS works.
		case GE_TEXMAP_UNKNOWN:
			if (g_Config.bPrescaleUV) {
				// We are here but are prescaling UV in the decoder? Let's do the same as in the other case
				// except consider *Scale and *Off to be 1 and 0.
				uvscaleoff[0] = widthFactor;
				uvscaleoff[1] = heightFactor;
				uvscaleoff[2] = 0.0f;
				uvscaleoff[3] = 0.0f;
			} else {
				uvscaleoff[0] = gstate_c.uv.uScale * factor * widthFactor;
				uvscaleoff[1] = gstate_c.uv.vScale * factor * heightFactor;
				uvscaleoff[2] = gstate_c.uv.uOff * widthFactor;
				uvscaleoff[3] = gstate_c.uv.vOff * heightFactor;
			}
			break;

		// These two work the same whether or not we prescale UV.

		case GE_TEXMAP_TEXTURE_MATRIX:
			// We cannot bake the UV coord scale factor in here, as we apply a matrix multiplication
			// before this is applied, and the matrix multiplication may contain translation. In this case
			// the translation will be scaled which breaks faces in Hexyz Force for example.
			// So I've gone back to applying the scale factor in the shader.
			uvscaleoff[0] = widthFactor;
			uvscaleoff[1] = heightFactor;
			uvscaleoff[2] = 0.0f;
			uvscaleoff[3] = 0.0f;
			break;

		case GE_TEXMAP_ENVIRONMENT_MAP:
			// In this mode we only use uvscaleoff to scale to the texture size.
			uvscaleoff[0] = widthFactor;
			uvscaleoff[1] = heightFactor;
			uvscaleoff[2] = 0.0f;
			uvscaleoff[3] = 0.0f;
			break;

		default:
			ERROR_LOG_REPORT(G3D, "Unexpected UV gen mode: %d", gstate.getUVGenMode());
		}
		glUniform4fv(u_uvscaleoffset, 1, uvscaleoff);
	}

	if ((dirty & DIRTY_TEXCLAMP) && u_texclamp != -1) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		// First wrap xy, then half texel xy (for clamp.)
		const float texclamp[4] = {
			widthFactor,
			heightFactor,
			invW * 0.5f,
			invH * 0.5f,
		};
		const float texclampoff[2] = {
			gstate_c.curTextureXOffset * invW,
			gstate_c.curTextureYOffset * invH,
		};
		glUniform4fv(u_texclamp, 1, texclamp);
		if (u_texclampoff != -1) {
			glUniform2fv(u_texclampoff, 1, texclampoff);
		}
	}

	// Transform
	if (dirty & DIRTY_WORLDMATRIX) {
		SetMatrix4x3(u_world, gstate.worldMatrix);
		position_changed = true;
	}
	if (dirty & DIRTY_VIEWMATRIX) {
		SetMatrix4x3(u_view, gstate.viewMatrix);
		Vec3 pos;
		for (int i = 0; i < 3; ++i)
			pos[i] = -gstate.viewMatrix[9 + i];
		if (pos.x == 0 && pos.y == 0 && pos.z == 0) {
			//NOTICE_LOG(VR, "Camera at origin");
			//ELOG("Camera at origin");
			if (g_Config.bDetectSkybox) g_is_skybox = true;
		} else {
			//NOTICE_LOG(VR, "Camera at %g, %g, %g", pos.x, pos.y, pos.z);
			//ELOG("Camera at %g, %g, %g; angle= %5.2f, %5.2f, %5.2f", pos.x, pos.y, pos.z, gstate.viewMatrix[0], gstate.viewMatrix[1], gstate.viewMatrix[2]);
			g_is_skybox = false;
		}
		position_changed = true;
	}
	if (dirty & DIRTY_TEXMATRIX) {
		SetMatrix4x3(u_texmtx, gstate.tgenMatrix);
	}
	if ((dirty & DIRTY_DEPTHRANGE) && u_depthRange != -1) {
		float viewZScale = gstate.getViewportZScale();
		float viewZCenter = gstate.getViewportZCenter();
		float viewZInvScale;

		// We had to scale and translate Z to account for our clamped Z range.
		// Therefore, we also need to reverse this to round properly.
		//
		// Example: scale = 65535.0, center = 0.0
		// Resulting range = -65535 to 65535, clamped to [0, 65535]
		// gstate_c.vpDepthScale = 2.0f
		// gstate_c.vpZOffset = -1.0f
		//
		// The projection already accounts for those, so we need to reverse them.
		//
		// Additionally, OpenGL uses a range from [-1, 1].  So we multiply by scale and add the center.
		viewZScale *= (1.0f / gstate_c.vpDepthScale);
		viewZCenter -= 65535.0f * (gstate_c.vpZOffset);

		if (viewZScale != 0.0) {
			viewZInvScale = 1.0f / viewZScale;
		} else {
			viewZInvScale = 0.0;
		}

		float data[4] = { viewZScale, viewZCenter, viewZCenter, viewZInvScale };
		SetFloatUniform4(u_depthRange, data);
	}

	if (position_changed && temp_skybox)
	{
		//g_is_skybox = false;
		temp_skybox = false;
		skybox_changed = true;
	}
	if (bViewportChanged)
	{
		bViewportChanged = false;
		// VR, Check whether it is a skybox, fullscreen, letterboxed, splitscreen multiplayer, hud element, or offscreen
		//SetViewportType(xfmem.viewport);
		//LogViewport(xfmem.viewport);

		//SetViewportConstants();

		// Update projection if the viewport isn't 1:1 useable
		//if (!g_ActiveConfig.backend_info.bSupportsOversizedViewports)
		//{
		//	ViewportCorrectionMatrix(s_viewportCorrection);
		//	skybox_changed = true;
		//}
		// VR adjust the projection matrix for the new kind of viewport
		//else if (g_viewport_type != g_old_viewport_type)
		//{
		//	skybox_changed = true;tr
		//}
	}
	if (position_changed && g_Config.bDetectSkybox && !g_is_skybox)
	{
		if (g_is_skybox)
		{
			temp_skybox = true;
			skybox_changed = true;
		}
	}
	if (!gstate.isModeThrough() && !isClear && (dirty & DIRTY_PROJMATRIX || bFreeLookChanged || (bFrameChanged && g_Config.bEnableVR && g_has_hmd))) {
		Matrix4x4 flippedMatrix;
		if (g_Config.bEnableVR && g_has_hmd) {
			flippedMatrix = SetProjectionConstants(gstate.projMatrix, dirty & DIRTY_PROJMATRIX, false);
			//bProjectionChanged = false;
		} else {
			memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

			const bool invertedY = gstate_c.vpHeight > 0;
			if (invertedY)
				flippedMatrix.flipAxis(1);
			const bool invertedX = gstate_c.vpWidth < 0;
			if (invertedX)
				flippedMatrix.flipAxis(0);

			// enable freelook also for non-VR mode, but only for perspective projections, not orthographic
			if (flippedMatrix.zw == -1.0f || flippedMatrix.zw == 1.0f) {
				Matrix4x4 free_look_matrix;
				Vec3 pos;
				float UnitsPerMetre = g_Config.fUnitsPerMetre / g_Config.fScale;
				for (int i = 0; i < 3; ++i)
					pos[i] = s_fViewTranslationVector[i] * UnitsPerMetre;
				free_look_matrix.setTranslation(pos);

				ScaleProjMatrix(flippedMatrix);
				flippedMatrix = free_look_matrix * flippedMatrix;
			}
		}
		bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
		if (useBufferedRendering)
			flippedMatrix.flipAxis(1);
		glUniformMatrix4fv(u_proj, 1, GL_FALSE, flippedMatrix.m);
		dirtyUniforms &= ~DIRTY_PROJMATRIX;
	}
	else if (skybox_changed && g_Config.bEnableVR && g_has_hmd)
	{
		Matrix4x4 flippedMatrix = SetProjectionConstants(gstate.projMatrix, false, false);
		glUniformMatrix4fv(u_proj, 1, GL_FALSE, flippedMatrix.m);
		dirtyUniforms &= ~DIRTY_PROJMATRIX;
	}
	if (gstate.isModeThrough() && !isClear && (dirty & DIRTY_PROJTHROUGHMATRIX || (bFrameChanged && g_Config.bEnableVR && g_has_hmd)))
	{
		Matrix4x4 proj_through;
		proj_through.setOrtho(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0.0f, 0.0f, 1.0f);
		//DEBUG_LOG(VR, "proj_through: (%d, %d) to (%d, %d), %g to %g", 0, 0, gstate_c.curRTWidth, gstate_c.curRTHeight, 0.0f, 1.0f);
		if (g_Config.bEnableVR && g_has_hmd)
			proj_through = SetProjectionConstants(proj_through.m, false, true);
		bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
		if (useBufferedRendering)
			proj_through.flipAxis(1);
		glUniformMatrix4fv(u_proj_through, 1, GL_FALSE, proj_through.m);
		dirtyUniforms &= ~DIRTY_PROJTHROUGHMATRIX;
	}
	bFrameChanged = false;

	//if (g_Config.iMotionSicknessSkybox == 2 && g_is_skybox)
	//	LockSkybox();

	if (dirty & DIRTY_STENCILREPLACEVALUE) {
		glUniform1f(u_stencilReplaceValue, (float)gstate.getStencilTestRef() * (1.0f / 255.0f));
	}
	// TODO: Could even set all bones in one go if they're all dirty.
#ifdef USE_BONE_ARRAY
	if (u_bone != -1) {
		float allBones[8 * 16];

		bool allDirty = true;
		for (int i = 0; i < numBones; i++) {
			if (dirty & (DIRTY_BONEMATRIX0 << i)) {
				ConvertMatrix4x3To4x4(allBones + 16 * i, gstate.boneMatrix + 12 * i);
			} else {
				allDirty = false;
			}
		}
		if (allDirty) {
			// Set them all with one call
			glUniformMatrix4fv(u_bone, numBones, GL_FALSE, allBones);
		} else {
			// Set them one by one. Could try to coalesce two in a row etc but too lazy.
			for (int i = 0; i < numBones; i++) {
				if (dirty & (DIRTY_BONEMATRIX0 << i)) {
					glUniformMatrix4fv(u_bone + i, 1, GL_FALSE, allBones + 16 * i);
				}
			}
		}
	}
#else
	float bonetemp[16];
	for (int i = 0; i < numBones; i++) {
		if (dirty & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To4x4(bonetemp, gstate.boneMatrix + 12 * i);
			glUniformMatrix4fv(u_bone[i], 1, GL_FALSE, bonetemp);
		}
	}
#endif

	if (dirty & DIRTY_SHADERBLEND) {
		if (u_blendFixA != -1) {
			SetColorUniform3(u_blendFixA, gstate.getFixA());
		}
		if (u_blendFixB != -1) {
			SetColorUniform3(u_blendFixB, gstate.getFixB());
		}

		const float fbotexSize[2] = {
			1.0f / (float)gstate_c.curRTRenderWidth,
			1.0f / (float)gstate_c.curRTRenderHeight,
		};
		if (u_fbotexSize != -1) {
			glUniform2fv(u_fbotexSize, 1, fbotexSize);
		}
	}

	// Lighting
	if (dirty & DIRTY_AMBIENT) {
		SetColorUniform3Alpha(u_ambient, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (dirty & DIRTY_MATAMBIENTALPHA) {
		SetColorUniform3Alpha(u_matambientalpha, gstate.materialambient, gstate.getMaterialAmbientA());
	}
	if (dirty & DIRTY_MATDIFFUSE) {
		SetColorUniform3(u_matdiffuse, gstate.materialdiffuse);
	}
	if (dirty & DIRTY_MATEMISSIVE) {
		SetColorUniform3(u_matemissive, gstate.materialemissive);
	}
	if (dirty & DIRTY_MATSPECULAR) {
		SetColorUniform3ExtraFloat(u_matspecular, gstate.materialspecular, getFloat24(gstate.materialspecularcoef));
	}

	for (int i = 0; i < 4; i++) {
		if (dirty & (DIRTY_LIGHT0 << i)) {
			if (gstate.isDirectionalLight(i)) {
				// Prenormalize
				float x = getFloat24(gstate.lpos[i * 3 + 0]);
				float y = getFloat24(gstate.lpos[i * 3 + 1]);
				float z = getFloat24(gstate.lpos[i * 3 + 2]);
				float len = sqrtf(x*x + y*y + z*z);
				if (len == 0.0f)
					len = 1.0f;
				else
					len = 1.0f / len;
				float vec[3] = { x * len, y * len, z * len };
				glUniform3fv(u_lightpos[i], 1, vec);
			} else {
				SetFloat24Uniform3(u_lightpos[i], &gstate.lpos[i * 3]);
			}
			if (u_lightdir[i] != -1) SetFloat24Uniform3(u_lightdir[i], &gstate.ldir[i * 3]);
			if (u_lightatt[i] != -1) SetFloat24Uniform3(u_lightatt[i], &gstate.latt[i * 3]);
			if (u_lightangle[i] != -1) glUniform1f(u_lightangle[i], getFloat24(gstate.lcutoff[i]));
			if (u_lightspotCoef[i] != -1) glUniform1f(u_lightspotCoef[i], getFloat24(gstate.lconv[i]));
			if (u_lightambient[i] != -1) SetColorUniform3(u_lightambient[i], gstate.lcolor[i * 3]);
			if (u_lightdiffuse[i] != -1) SetColorUniform3(u_lightdiffuse[i], gstate.lcolor[i * 3 + 1]);
			if (u_lightspecular[i] != -1) SetColorUniform3(u_lightspecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}
	return dirtyUniforms;
}


// The PSP only has a 16 bit z buffer, but we have a 24 bit z buffer, meaning znear can sometimes be almost 256 times closer with no loss of precision.
// Unfortunately, dividing by 80 breaks Armored Core 3, so lets just divide by 20. We should probably check zfar too.
// We might need more precision than a PSP. But we never need closer than 2 cm (unless the game renders that close).
#define GetBetterZNear(znear) fmax(znear / 20.0f, 0.02f * UnitsPerMetre)

Matrix4x4 LinkedShader::SetProjectionConstants(float input_proj_matrix[], bool shouldLog, bool isThrough) {
	float stereoparams[4];
	char s[1024];
	Matrix4x4 flippedMatrix;
	memcpy(&flippedMatrix, input_proj_matrix, 16 * sizeof(float));

	if (debug_newScene) {
		//flippedMatrix.toOpenGL(s, 1024);
		//NOTICE_LOG(VR, "input: %s", s);
	}

	bool isPerspective = flippedMatrix.zw == -1.0f || flippedMatrix.zw == 1.0f;

	if (!isThrough) {
		const bool invertedY = gstate_c.vpHeight > 0;
		if (invertedY)
			flippedMatrix.flipAxis(1);
		const bool invertedX = gstate_c.vpWidth < 0;
		if (invertedX) 
			flippedMatrix.flipAxis(0);
	}

	if (debug_newScene) {
		//flippedMatrix.toOpenGL(s, 1024);
		//NOTICE_LOG(VR, "flipped: %s", s);
	}
	if (shouldLog)
		LogProj(flippedMatrix);

	float gameLeft, gameRight, gameBottom, gameTop, gameZNear, gameZFar, gameHFOV, gameVFOV;
	bool gameFlipX = false, gameFlipY = false, gameFlipZ;
	flippedMatrix.getOpenGLProjection(&gameLeft, &gameRight, &gameBottom, &gameTop, &gameZNear, &gameZFar, &gameHFOV, &gameVFOV, &gameFlipZ);
	if (gameHFOV < 0) {
		gameFlipX = true;
		gameHFOV = -gameHFOV;
	}
	if (gameVFOV < 0) {
		gameFlipY = true;
		gameVFOV = -gameVFOV;
	}

	///////////////////////////////////////////////////////
	// First, identify any special layers and hacks

	m_layer_on_top = false;
	bool bFullscreenLayer = g_Config.bHudFullscreen && !isPerspective;
	bool bFlashing = (debug_projNum - 1) == g_Config.iSelectedLayer;
	bool bStuckToHead = false, bHide = false;
	int iTelescopeHack = -1;
	float fScaleHack = 1, fWidthHack = 1, fHeightHack = 1, fUpHack = 0, fRightHack = 0;

	//if (g_Config.iMetroidPrime)
	//{
		//GetMetroidPrimeValues(&bStuckToHead, &bFullscreenLayer, &bHide, &bFlashing,
		//	&fScaleHack, &fWidthHack, &fHeightHack, &fUpHack, &fRightHack, &iTelescopeHack);
	//}

	if (g_Config.bBefore3DIsBackground && last_frame_had_3d && !isPerspective && !this_frame_has_3d)
	{
		//HACK_LOG(VR, "Before 3D: Background");
		g_is_skyplane = true;
	}
	else if (g_Config.bBefore3DIsBackground) {
		g_is_skyplane = false;
	}

	bool isSkybox = (g_is_skybox && isPerspective) || g_is_skyplane;

	//if (isThrough) {
	//	flippedMatrix.toOpenGL(s, 1024);
	//	ELOG("Through: %s", s);
	//} else {
	//	flippedMatrix.toOpenGL(s, 1024);
	//	ELOG("%s", s);
	//}

	//if (isSkybox && !isThrough)
	//	bHide = true;

	// VR: in split-screen, only draw VR player TODO: fix offscreen to render to a separate texture in VR 
	bHide = bHide || (g_has_hmd && g_Config.bEnableVR && (g_viewport_type == VIEW_OFFSCREEN || (g_viewport_type >= VIEW_PLAYER_1 && g_viewport_type <= VIEW_PLAYER_4 && g_Config.iVRPlayer != g_viewport_type - VIEW_PLAYER_1)));
	// flash selected layer for debugging
	bHide = bHide || (bFlashing && g_Config.iFlashState > 5);
	// hide skybox or everything to reduce motion sickness
	bHide = bHide || (isSkybox && g_Config.iMotionSicknessSkybox == 1) || g_vr_black_screen;

	// Split WidthHack and HeightHack into left and right versions for telescopes
	float fLeftWidthHack = fWidthHack, fRightWidthHack = fWidthHack;
	float fLeftHeightHack = fHeightHack, fRightHeightHack = fHeightHack;
	bool bHideLeft = bHide, bHideRight = bHide, bTelescopeHUD = false, bNoForward = false;
	if (iTelescopeHack < 0 && g_Config.iTelescopeEye && vr_widest_3d_VFOV <= g_Config.fTelescopeMaxFOV && vr_widest_3d_VFOV > 1
		&& (g_Config.fTelescopeMaxFOV <= g_Config.fMinFOV || (g_Config.fTelescopeMaxFOV > g_Config.fMinFOV && vr_widest_3d_VFOV > g_Config.fMinFOV)))
		iTelescopeHack = g_Config.iTelescopeEye;
	if (g_has_hmd && g_Config.bEnableVR && iTelescopeHack > 0)
	{
		bNoForward = true;
		// Calculate telescope scale
		float hmd_halftan, telescope_scale;
		VR_GetProjectionHalfTan(hmd_halftan);
		telescope_scale = fabs(hmd_halftan / tan(DEGREES_TO_RADIANS(vr_widest_3d_VFOV) / 2));
		if (iTelescopeHack & 1)
		{
			fLeftWidthHack *= telescope_scale;
			fLeftHeightHack *= telescope_scale;
			bHideLeft = false;
		}
		if (iTelescopeHack & 2)
		{
			fRightWidthHack *= telescope_scale;
			fRightHeightHack *= telescope_scale;
			bHideRight = false;
		}
	}

	///////////////////////////////////////////////////////
	// What happens last depends on what kind of rendering we are doing for this layer
	// Hide: don't render anything
	// Render to texture: render in 2D exactly the same as the real console would, for projection shadows etc.
	// Free Look
	// Normal emulation
	// VR Fullscreen layer: render EFB copies or screenspace effects so they fill the full screen they were copied from
	// VR: Render everything as part of a virtual world, there are a few separate alternatives here:
	//     2D HUD as thick pane of glass floating in 3D space
	//     3D HUD element as a 3D object attached to that pane of glass
	//     3D world

	float UnitsPerMetre = g_Config.fUnitsPerMetre * fScaleHack / g_Config.fScale;

	bHide = bHide && (bFlashing || (g_has_hmd && g_Config.bEnableVR));

	if (bHide)
	{
		// If we are supposed to hide the layer, zero out the projection matrix
		Matrix4x4 final_matrix;
		final_matrix.empty();
		memset(stereoparams, 0, sizeof(stereoparams));
		glUniform4fv(u_StereoParams, 1, stereoparams);
		return final_matrix;
	}
	// don't do anything fancy for rendering to a texture
	// render exactly as we are told, and in mono
	else if (g_viewport_type == VIEW_RENDER_TO_TEXTURE)
	{
		// we aren't applying viewport correction, because Render To Texture never has a viewport larger than the framebufffer
		memset(stereoparams, 0, sizeof(stereoparams));
		glUniform4fv(u_StereoParams, 1, stereoparams);
		return flippedMatrix;
	}
	// This was already copied from the fullscreen EFB.
	// Which makes it already correct for the HMD's FOV.
	// But we still need to correct it for the difference between the requested and rendered viewport.
	// Don't add any stereoscopy because that was already done when copied.
	else if (bFullscreenLayer)
	{
		ScaleProjMatrix(flippedMatrix);
		memset(stereoparams, 0, sizeof(stereoparams));
		glUniform4fv(u_StereoParams, 1, stereoparams);
		return flippedMatrix;
	}

	// VR HMD 3D projection matrix, needs to include head-tracking

	// near clipping plane in game units
	float zfar, znear, zNearBetter, hfov, vfov;

	// if the camera is zoomed in so much that the action only fills a tiny part of your FOV,
	// we need to move the camera forwards until objects at AimDistance fill the minimum FOV.
	float zoom_forward = 0.0f;
	if (vr_widest_3d_HFOV <= g_Config.fMinFOV && vr_widest_3d_HFOV > 0 && iTelescopeHack <= 0)
	{
		zoom_forward = g_Config.fAimDistance * tanf(DEGREES_TO_RADIANS(g_Config.fMinFOV) / 2) / tanf(DEGREES_TO_RADIANS(vr_widest_3d_HFOV) / 2);
		zoom_forward -= g_Config.fAimDistance;
	}

	// Real 3D scene
	if (isPerspective && g_viewport_type != VIEW_HUD_ELEMENT && g_viewport_type != VIEW_OFFSCREEN)
	{
		this_frame_has_3d = true;

		znear = gameZNear;
		zfar = gameZFar;
		hfov = gameHFOV;
		vfov = gameVFOV;

		zNearBetter = GetBetterZNear(znear);

		// Find the game's camera angle and position by looking at the view/model matrix of the first real 3D object drawn.
		// This won't work for all games.
		if (!g_vr_had_3D_already) {
			CheckOrientationConstants();
			g_vr_had_3D_already = true;
		}
	}
	// 2D layer we will turn into a 3D scene
	// or 3D HUD element that we will treat like a part of the 2D HUD 
	else
	{
		//if (isThrough && isSkybox)
		//	ELOG("2D Skybox Through");
		//else if (isSkybox)
		//	ELOG("2D Skybox");
		//else if (isThrough)
		//	ELOG("2D Through");
		//else
		//	ELOG("2D");
		m_layer_on_top = g_Config.bHudOnTop;
		if (vr_widest_3d_HFOV > 0)
		{
			znear = vr_widest_3d_zNear;
			zfar = vr_widest_3d_zFar;
			if (zoom_forward != 0)
			{
				hfov = g_Config.fMinFOV;
				vfov = g_Config.fMinFOV * vr_widest_3d_VFOV / vr_widest_3d_HFOV;
			}
			else
			{
				hfov = vr_widest_3d_HFOV;
				vfov = vr_widest_3d_VFOV;
			}
			//if (debug_newScene)
			//	NOTICE_LOG(VR, "2D to fit 3D world: hfov=%8.4f    vfov=%8.4f      znear=%8.4f   zfar=%8.4f", hfov, vfov, znear, zfar);
			zNearBetter = GetBetterZNear(znear);
		}
		else
		{
			// default, if no 3D in scene
			znear = 0.02f * UnitsPerMetre; // 2 cm
			zfar = 500 * UnitsPerMetre; // 500 m
			hfov = 70; // 70 degrees
			vfov = 180.0f / 3.14159f * 2 * atanf(tanf((hfov*3.14159f / 180.0f) / 2)* 9.0f / 16.0f); // 2D screen is always meant to be 16:9 aspect ratio
			zNearBetter = znear;
			// TODO: fix aspect ratio in portrait mode
			//if (debug_newScene)
			//	NOTICE_LOG(VR, "Only 2D Projecting: %g x %g, n=%fm f=%fm", hfov, vfov, znear, zfar);
		}
		//if (debug_newScene)
		//	NOTICE_LOG(VR, "2D: zNear3D = %f, znear = %f, zFar = %f", zNear3D, znear, zfar);
	}

	Matrix44 proj_left, proj_right, hmd_left, hmd_right, temp;
	VR_GetProjectionMatrices(temp, hmd_right, zNearBetter, zfar, true);
	hmd_left = temp.transpose();
	temp = hmd_right;
	hmd_right = temp.transpose();
	proj_left = hmd_left;
	proj_right = hmd_right;
	stereoparams[0] = proj_left.xx;
	stereoparams[1] = proj_right.xx;
	stereoparams[2] = proj_left.zx;
	stereoparams[3] = proj_right.zx;

	float hfov2 = 2 * atan(1.0f / hmd_left.data[0 * 4 + 0])*180.0f / 3.1415926535f;
	float vfov2 = 2 * atan(1.0f / hmd_left.data[1 * 4 + 1])*180.0f / 3.1415926535f;
	float zfar2 = hmd_left.wz / hmd_left.zz;
	float znear2 = (1 + hmd_left.zz * zfar) / hmd_left.zz;
	if (debug_newScene)
	{
		//hmd_left.toOpenGL(s, 1024);
		//WARN_LOG(VR, "hmd_left: %s", s);

		// yellow = HMD's suggestion
		//WARN_LOG(VR, "O hfov=%8.4f    vfov=%8.4f      znear=%8.4f   zfar=%8.4f", hfov2, vfov2, znear2, zfar2);
		//WARN_LOG(VR, "O [%8.4f %8.4f %8.4f   %8.4f]", hmd_left.data[0 * 4 + 0], hmd_left.data[0 * 4 + 1], hmd_left.data[0 * 4 + 2], hmd_left.data[0 * 4 + 3]);
		//WARN_LOG(VR, "O [%8.4f %8.4f %8.4f   %8.4f]", hmd_left.data[1 * 4 + 0], hmd_left.data[1 * 4 + 1], hmd_left.data[1 * 4 + 2], hmd_left.data[1 * 4 + 3]);
		//WARN_LOG(VR, "O [%8.4f %8.4f %8.4f   %8.4f]", hmd_left.data[2 * 4 + 0], hmd_left.data[2 * 4 + 1], hmd_left.data[2 * 4 + 2], hmd_left.data[2 * 4 + 3]);
		//WARN_LOG(VR, "O {%8.4f %8.4f %8.4f   %8.4f}", hmd_left.data[3 * 4 + 0], hmd_left.data[3 * 4 + 1], hmd_left.data[3 * 4 + 2], hmd_left.data[3 * 4 + 3]);
		// green = Game's suggestion
		//INFO_LOG(VR, "G [%8.4f %8.4f %8.4f   %8.4f]", flippedMatrix.data[0 * 4 + 0], flippedMatrix.data[0 * 4 + 1], flippedMatrix.data[0 * 4 + 2], flippedMatrix.data[0 * 4 + 3]);
		//INFO_LOG(VR, "G [%8.4f %8.4f %8.4f   %8.4f]", flippedMatrix.data[1 * 4 + 0], flippedMatrix.data[1 * 4 + 1], flippedMatrix.data[1 * 4 + 2], flippedMatrix.data[1 * 4 + 3]);
		//INFO_LOG(VR, "G [%8.4f %8.4f %8.4f   %8.4f]", flippedMatrix.data[2 * 4 + 0], flippedMatrix.data[2 * 4 + 1], flippedMatrix.data[2 * 4 + 2], flippedMatrix.data[2 * 4 + 3]);
		//INFO_LOG(VR, "G {%8.4f %8.4f %8.4f   %8.4f}", flippedMatrix.data[3 * 4 + 0], flippedMatrix.data[3 * 4 + 1], flippedMatrix.data[3 * 4 + 2], flippedMatrix.data[3 * 4 + 3]);
	}
	// red = my combination
	proj_left.xx = hmd_left.xx * SignOf(proj_left.xx) * fLeftWidthHack; // h fov
	proj_left.yy = hmd_left.yy * SignOf(proj_left.yy) * fLeftHeightHack; // v fov
	proj_left.zx = hmd_left.zx * SignOf(proj_left.xx) - fRightHack; // h off-axis
	proj_left.zy = hmd_left.zy * SignOf(proj_left.yy) - fUpHack; // v off-axis
	proj_right.xx = hmd_right.xx * SignOf(proj_right.xx) * fLeftWidthHack; // h fov
	proj_right.yy = hmd_right.yy * SignOf(proj_right.yy) * fLeftHeightHack; // v fov
	proj_right.zx = hmd_right.zx * SignOf(proj_right.xx) - fRightHack; // h off-axis
	proj_right.zy = hmd_right.zy * SignOf(proj_right.yy) - fUpHack; // v off-axis

	//if (g_ActiveConfig.backend_info.bSupportsGeometryShaders)
	{
		proj_left.zx = 0;
	}

	if (debug_newScene) {
		//proj_left.toOpenGL(s, 1024);
		//ERROR_LOG(VR, "mine: %s", s);
	}

	if (debug_newScene)
	{
		//DEBUG_LOG(VR, "VR [%8.4f %8.4f %8.4f   %8.4f]", proj_left.data[0 * 4 + 0], proj_left.data[0 * 4 + 1], proj_left.data[0 * 4 + 2], proj_left.data[0 * 4 + 3]);
		//DEBUG_LOG(VR, "VR [%8.4f %8.4f %8.4f   %8.4f]", proj_left.data[1 * 4 + 0], proj_left.data[1 * 4 + 1], proj_left.data[1 * 4 + 2], proj_left.data[1 * 4 + 3]);
		//DEBUG_LOG(VR, "VR [%8.4f %8.4f %8.4f   %8.4f]", proj_left.data[2 * 4 + 0], proj_left.data[2 * 4 + 1], proj_left.data[2 * 4 + 2], proj_left.data[2 * 4 + 3]);
		//DEBUG_LOG(VR, "VR {%8.4f %8.4f %8.4f   %8.4f}", proj_left.data[3 * 4 + 0], proj_left.data[3 * 4 + 1], proj_left.data[3 * 4 + 2], proj_left.data[3 * 4 + 3]);
	}

	//VR Headtracking and leaning back compensation
	Matrix44 rotation_matrix;
	Matrix44 lean_back_matrix;
	Matrix44 camera_pitch_matrix;
	if (bStuckToHead)
	{
		Matrix44::LoadIdentity(rotation_matrix);
		Matrix44::LoadIdentity(lean_back_matrix);
		Matrix44::LoadIdentity(camera_pitch_matrix);
	}
	else
	{
		// head tracking
		if (g_Config.bOrientationTracking)
		{
			g_framebufferManager->UpdateHeadTrackingIfNeeded();
			rotation_matrix = g_head_tracking_matrix.transpose();
		}
		else
		{
			rotation_matrix.setIdentity();
		}

		// leaning back
		float extra_pitch = -g_Config.fLeanBackAngle;
		lean_back_matrix.setRotationX(-DEGREES_TO_RADIANS(extra_pitch));
		// camera pitch + camera stabilsation
		if ((g_Config.bStabilizePitch || g_Config.bStabilizeRoll || g_Config.bStabilizeYaw) && g_Config.bCanReadCameraAngles && (g_Config.iMotionSicknessSkybox != 2 || !isSkybox))
		{
			if (!g_Config.bStabilizePitch)
			{
				Matrix44 user_pitch44;

				if (isPerspective || vr_widest_3d_HFOV > 0)
					extra_pitch = g_Config.fCameraPitch;
				else
					extra_pitch = g_Config.fScreenPitch;
				user_pitch44.setRotationX(-DEGREES_TO_RADIANS(extra_pitch));
				camera_pitch_matrix = g_game_camera_rotmat * user_pitch44; // or vice versa?
			}
			else
			{
				camera_pitch_matrix = g_game_camera_rotmat;
			}
		}
		else
		{
			if (isPerspective || vr_widest_3d_HFOV > 0)
				extra_pitch = g_Config.fCameraPitch;
			else
				extra_pitch = g_Config.fScreenPitch;
			camera_pitch_matrix.setRotationX(-DEGREES_TO_RADIANS(extra_pitch));
		}
	}

	// Position matrices
	Matrix44 head_position_matrix, free_look_matrix, camera_forward_matrix, camera_position_matrix;
	if (bStuckToHead || isSkybox)
	{
		Matrix44::LoadIdentity(head_position_matrix);
		Matrix44::LoadIdentity(free_look_matrix);
		Matrix44::LoadIdentity(camera_position_matrix);
	}
	else
	{
		Vec3 pos;
		// head tracking
		if (g_Config.bPositionTracking)
		{
			for (int i = 0; i < 3; ++i)
				pos[i] = g_head_tracking_position[i] * UnitsPerMetre;
			head_position_matrix.setTranslation(pos);
		}
		else
		{
			head_position_matrix.setIdentity();
		}

		// freelook camera position
		for (int i = 0; i < 3; ++i)
			pos[i] = s_fViewTranslationVector[i] * UnitsPerMetre;
		free_look_matrix.setTranslation(pos);

		// camera position stabilisation
		if (g_Config.bStabilizeX || g_Config.bStabilizeY || g_Config.bStabilizeZ)
		{
			for (int i = 0; i < 3; ++i)
				pos[i] = -g_game_camera_pos[i] * UnitsPerMetre;
			camera_position_matrix.setTranslation(pos);
		}
		else
		{
			camera_position_matrix.setIdentity();
		}
	}

	Matrix44 look_matrix;
	if (isPerspective && g_viewport_type != VIEW_HUD_ELEMENT && g_viewport_type != VIEW_OFFSCREEN)
	{
		// Transformations must be applied in the following order for VR:
		// camera position stabilisation
		// camera forward
		// camera pitch
		// free look
		// leaning back
		// head position tracking
		// head rotation tracking
		if (bNoForward || isSkybox || bStuckToHead)
		{
			camera_forward_matrix.setIdentity();
		}
		else
		{
			Vec3 pos;
			pos[0] = 0;
			pos[1] = 0;
			pos[2] = (g_Config.fCameraForward + zoom_forward) * UnitsPerMetre;
			camera_forward_matrix.setTranslation(pos);
		}

		look_matrix = camera_forward_matrix * camera_position_matrix * camera_pitch_matrix * free_look_matrix * lean_back_matrix * head_position_matrix * rotation_matrix;
		if (gameFlipX || gameFlipY || gameFlipZ) {
			Matrix4x4 scale;
			scale.setScaling(Vec3(gameFlipX ? -1.0f : 1.0f, gameFlipY ? -1.0f : 1.0f, gameFlipZ ? -1.0f : 1.0f));
			look_matrix = scale * look_matrix;
		}

		if (g_Config.iInternalScreenRotation != ROTATION_LOCKED_HORIZONTAL) {
			Matrix4x4 portrait;
			portrait.setRotationZ(DEGREES_TO_RADIANS(90));
			look_matrix = portrait * look_matrix;
		}

	}
	else
		//if (xfmem.projection.type != GX_PERSPECTIVE || g_viewport_type == VIEW_HUD_ELEMENT || g_viewport_type == VIEW_OFFSCREEN)
	{
		//if (debug_newScene)
		//	INFO_LOG(VR, "2D: hacky test");

		float HudWidth, HudHeight, HudThickness, HudDistance, HudUp, CameraForward, AimDistance;

		// 2D Screen
		if (vr_widest_3d_HFOV <= 0)
		{
			if (isSkybox) {
				HudThickness = 0;
				HudDistance = zfar*2;
				HudWidth = HudHeight = HudDistance * 2; // 90 degree skycube
			} else {
				HudThickness = g_Config.fScreenThickness * UnitsPerMetre;
				HudDistance = g_Config.fScreenDistance * UnitsPerMetre;
				HudHeight = g_Config.fScreenHeight * UnitsPerMetre;
				HudHeight = g_Config.fScreenHeight * UnitsPerMetre;
				HudWidth = HudHeight * (float)16 / 9;
			}
			CameraForward = 0;
			HudUp = g_Config.fScreenUp * UnitsPerMetre;
			AimDistance = HudDistance;
		}
		else
			// HUD over 3D world
		{
			if (isSkybox) {
				HudThickness = 0;
				HudDistance = zfar*2;
				// It might be better to use 90 degrees for the hfov (but not vfov), we should test what looks best
				HudWidth  = 2.0f * tanf(DEGREES_TO_RADIANS(hfov / 2.0f)) * HudDistance;
				HudHeight = 2.0f * tanf(DEGREES_TO_RADIANS(vfov / 2.0f)) * HudDistance;
				CameraForward = 0;
			}
			else {
				// Give the 2D layer a 3D effect if different parts of the 2D layer are rendered at different z coordinates
				HudThickness = g_Config.fHudThickness * UnitsPerMetre;  // the 2D layer is actually a 3D box this many game units thick
				HudDistance = g_Config.fHudDistance * UnitsPerMetre;   // depth 0 on the HUD should be this far away
				HudUp = 0;
				if (bNoForward)
					CameraForward = 0;
				else
					CameraForward = (g_Config.fCameraForward + zoom_forward) * UnitsPerMetre;
				// When moving the camera forward, correct the size of the HUD so that aiming is correct at AimDistance
				AimDistance = g_Config.fAimDistance * UnitsPerMetre;
				if (AimDistance <= 0)
					AimDistance = HudDistance;
				// Now that we know how far away the box is, and what FOV it should fill, we can work out the width and height in game units
				// Note: the HUD won't line up exactly (except at AimDistance) if CameraForward is non-zero 
				//float HudWidth = 2.0f * tanf(hfov / 2.0f * 3.14159f / 180.0f) * (HudDistance) * Correction;
				//float HudHeight = 2.0f * tanf(vfov / 2.0f * 3.14159f / 180.0f) * (HudDistance) * Correction;
				HudWidth = 2.0f * tanf(DEGREES_TO_RADIANS(hfov / 2.0f)) * HudDistance * (AimDistance + CameraForward) / AimDistance;
				HudHeight = 2.0f * tanf(DEGREES_TO_RADIANS(vfov / 2.0f)) * HudDistance * (AimDistance + CameraForward) / AimDistance;
			}
		}

		Vec3 scale; // width, height, and depth of box in game units divided by 2D width, height, and depth 
		Vec3 position; // position of front of box relative to the camera, in game units 

		float viewport_scale[2];
		float viewport_offset[2]; // offset as a fraction of the viewport's width
		//if (g_viewport_type != VIEW_HUD_ELEMENT && g_viewport_type != VIEW_OFFSCREEN)
		{
			viewport_scale[0] = 1.0f;
			viewport_scale[1] = 1.0f;
			viewport_offset[0] = 0.0f;
			viewport_offset[1] = 0.0f;
		}
		//else
		//{
		//	Viewport &v = xfmem.viewport;
		//	float left, top, width, height;
		//	left = v.xOrig - v.wd - 342;
		//	top = v.yOrig + v.ht - 342;
		//	width = 2 * v.wd;
		//	height = -2 * v.ht;
		//	float screen_width = (float)g_final_screen_region.GetWidth();
		//	float screen_height = (float)g_final_screen_region.GetHeight();
		//	viewport_scale[0] = width / screen_width;
		//	viewport_scale[1] = height / screen_height;
		//	viewport_offset[0] = ((left + (width / 2)) - (0 + (screen_width / 2))) / screen_width;
		//	viewport_offset[1] = -((top + (height / 2)) - (0 + (screen_height / 2))) / screen_height;
		//}

		// 3D HUD elements (may be part of 2D screen or HUD)
		if (isPerspective)
		{
			// these are the edges of the near clipping plane in game coordinates
			float left2D = -(flippedMatrix.zx + 1) / flippedMatrix.xx;
			float right2D = left2D + 2 / flippedMatrix.xx;
			float bottom2D = -(flippedMatrix.zy + 1) / flippedMatrix.yy;
			float top2D = bottom2D + 2 / flippedMatrix.yy;
			//OpenGL: f = (1 - wz) / zz; n = (-1 - wz) / zz
			float zFar2D = (1 - flippedMatrix.wz) / flippedMatrix.zz;
			float zNear2D = (-1 - flippedMatrix.wz) / flippedMatrix.zz;
			float zObj = zNear2D + (zFar2D - zNear2D) * g_Config.fHud3DCloser;

			//HACK_LOG(VR, "3D HUD: (%g, %g) to (%g, %g); z: %g to %g, %g", left2D, top2D, right2D, bottom2D, zNear2D, zFar2D, zObj);


			left2D *= zObj;
			right2D *= zObj;
			bottom2D *= zObj;
			top2D *= zObj;



			// Scale the width and height to fit the HUD in metres
			if (flippedMatrix.xx == 0 || right2D == left2D) {
				scale[0] = 0;
			}
			else {
				scale[0] = viewport_scale[0] * HudWidth / (right2D - left2D);
			}
			if (flippedMatrix.yy == 0 || top2D == bottom2D) {
				scale[1] = 0;
			}
			else {
				scale[1] = viewport_scale[1] * HudHeight / (top2D - bottom2D); // note that positive means up in 3D
			}
			// Keep depth the same scale as width, so it looks like a real object
			if (flippedMatrix.zz == 0 || zFar2D == zNear2D) {
				scale[2] = scale[0];
			}
			else {
				scale[2] = scale[0];
			}
			// Adjust the position for off-axis projection matrices, and shifting the 2D screen
			position[0] = scale[0] * (-(right2D + left2D) / 2.0f) + viewport_offset[0] * HudWidth; // shift it right into the centre of the view
			position[1] = scale[1] * (-(top2D + bottom2D) / 2.0f) + viewport_offset[1] * HudHeight + HudUp; // shift it up into the centre of the view;
			// Shift it from the old near clipping plane to the HUD distance, and shift the camera forward
			if (vr_widest_3d_HFOV <= 0)
				position[2] = scale[2] * zObj - HudDistance;
			else
				position[2] = scale[2] * zObj - HudDistance; // - CameraForward;
		}
		// 2D layer, or 2D viewport (may be part of 2D screen or HUD)
		else
		{
			float left2D = gameLeft;
			float right2D = gameRight;
			float bottom2D = gameBottom;
			float top2D = gameTop;
			float zFar2D, zNear2D;
			zFar2D = gameZFar;
			zNear2D = gameZNear;

			// proj_through
			//NOTICE_LOG(VR, "2D: (%g, %g) to (%g, %g), %g to %g", left2D, top2D, right2D, bottom2D, zNear2D, zFar2D);
			//NOTICE_LOG(VR, "HUDWidth = %g, HudHeight = %g, HudThickness = %g, HudDistance = %g", HudWidth, HudHeight, HudThickness, HudDistance);

			// for 2D, work out the fraction of the HUD we should fill, and multiply the scale by that
			// also work out what fraction of the height we should shift it up, and what fraction of the width we should shift it left
			// only multiply by the extra scale after adjusting the position?

			if (flippedMatrix.xx == 0 || right2D == left2D) {
				scale[0] = 0;
			}
			else {
				scale[0] = viewport_scale[0] * HudWidth / (right2D - left2D);
			}
			if (flippedMatrix.yy == 0 || top2D == bottom2D) {
				scale[1] = 0;
			}
			else {
				scale[1] = viewport_scale[1] * HudHeight / (top2D - bottom2D); // note that positive means up in 3D
			}
			if (zFar2D == zNear2D) {
				scale[2] = 0; // The 2D layer was flat, so we make it flat instead of a box to avoid dividing by zero
			}
			else {
				scale[2] = HudThickness / (zFar2D - zNear2D); // Scale 2D z values into 3D game units so it is the right thickness
				if ((isThrough && g_Config.bInvert2DThroughDepth) || (!isThrough && g_Config.bInvert2DOrthoDepth))
					scale[2] = -scale[2];
			}
			position[0] = scale[0] * (-(right2D + left2D) / 2.0f) + viewport_offset[0] * HudWidth; // shift it right into the centre of the view
			position[1] = scale[1] * (-(top2D + bottom2D) / 2.0f) + viewport_offset[1] * HudHeight + HudUp; // shift it up into the centre of the view;
			// Shift it from the zero plane to the HUD distance, and shift the camera forward
			if (vr_widest_3d_HFOV <= 0)
				position[2] = -HudDistance;
			else
				position[2] = -HudDistance; // - CameraForward;
			//NOTICE_LOG(VR, "2D: Scale: [x%g, x%g, x%g]; Pos: (%g, %g, %g)", scale[0], scale[1], scale[2], position[0], position[1], position[2]);
		}

		Matrix44 scale_matrix, position_matrix;
		scale_matrix.setScaling(scale);
		position_matrix.setTranslation(position);

		if (g_Config.iInternalScreenRotation != ROTATION_LOCKED_HORIZONTAL) {
			Matrix4x4 portrait;
			portrait.setRotationZ(DEGREES_TO_RADIANS(90));
			look_matrix = scale_matrix * position_matrix * portrait * camera_position_matrix * camera_pitch_matrix * free_look_matrix * lean_back_matrix * head_position_matrix * rotation_matrix;
		}
		else {
			// order: scale, position
			look_matrix = scale_matrix * position_matrix * camera_position_matrix * camera_pitch_matrix * free_look_matrix * lean_back_matrix * head_position_matrix * rotation_matrix;
			//look_matrix = camera_position_matrix * camera_pitch_matrix * free_look_matrix * lean_back_matrix * head_position_matrix * rotation_matrix;
			//look_matrix = scale_matrix * head_position_matrix;
		}
	}

	Matrix44 eye_pos_matrix_left, eye_pos_matrix_right;
	float posLeft[3] = { 0, 0, 0 };
	float posRight[3] = { 0, 0, 0 };
	if (!isSkybox)
	{
		VR_GetEyePos(posLeft, posRight);
		for (int i = 0; i < 3; ++i)
		{
			posLeft[i] *= UnitsPerMetre;
			posRight[i] *= UnitsPerMetre;
		}
	}
	stereoparams[0] *= posLeft[0];
	stereoparams[1] *= posRight[0];

	Matrix44 view_matrix_left, view_matrix_right;
	//if (g_Config.backend_info.bSupportsGeometryShaders)
	{
		Matrix44::Set(view_matrix_left, look_matrix.data);
		Matrix44::Set(view_matrix_right, view_matrix_left.data);
	}
	//else
	//{
	//	Matrix44::Translate(eye_pos_matrix_left, posLeft);
	//	Matrix44::Translate(eye_pos_matrix_right, posRight);
	//	Matrix44::Multiply(eye_pos_matrix_left, look_matrix, view_matrix_left);
	//	Matrix44::Multiply(eye_pos_matrix_right, look_matrix, view_matrix_right);
	//}
	Matrix44 final_matrix_left, final_matrix_right;
	//Matrix44::Multiply(proj_left, view_matrix_left, final_matrix_left);
	//Matrix44::Multiply(proj_right, view_matrix_right, final_matrix_right);
	final_matrix_left = view_matrix_left * proj_left;

	if (debug_newScene) {
		//final_matrix_left.toOpenGL(s, 1024);
		//INFO_LOG(VR, "final: %s", s);
	}

	if (!isThrough)
		ScaleProjMatrix(final_matrix_left);
	glUniform4fv(u_StereoParams, 1, stereoparams);
	return final_matrix_left;
}

void LinkedShader::CheckOrientationConstants()
{
#define sqr(a) ((a)*(a))
	if (g_Config.bCanReadCameraAngles && (g_Config.bStabilizePitch || g_Config.bStabilizeRoll || g_Config.bStabilizeYaw || g_Config.bStabilizeX || g_Config.bStabilizeY || g_Config.bStabilizeZ))
	{
		float *p = gstate.viewMatrix;
		//Vec3 pos;
		//for (int i = 0; i < 3; ++i)
		//	pos[i] = gstate.viewMatrix[9 + i];
		Matrix3x3 rot;
		rot.setIdentity();
		memcpy(&rot.data[0 * 3], &p[0 * 3], 3 * sizeof(float));
		memcpy(&rot.data[1 * 3], &p[1 * 3], 3 * sizeof(float));
		memcpy(&rot.data[2 * 3], &p[2 * 3], 3 * sizeof(float));

		float scale = sqrt(sqr(rot.data[0 * 3 + 0]) + sqr(rot.data[0 * 3 + 1]) + sqr(rot.data[0 * 3 + 2]));
		for (int r = 0; r < 3; ++r)
			for (int c = 0; c < 3; ++c)
				rot.data[r * 3 + c] /= scale;

		// add pitch to rotation matrix
		if (g_Config.fReadPitch != 0)
		{
			Matrix3x3 rp;
			rp.setRotationX(DEGREES_TO_RADIANS(g_Config.fReadPitch));
			rot = rp * rot;
		}

		rot = rot.transpose();

		// extract yaw, pitch, and roll in RADIANS from rotation matrix
		float yaw, pitch, roll;
		Matrix33::GetPieYawPitchRollR(rot, yaw, pitch, roll);

		// if it thinks the camera is upside down, it's probably just a menu and we shouldn't use stabilisation
		if (fabs(roll) > 160) {
			g_game_camera_rotmat.setIdentity();
			memset(g_game_camera_pos, 0, 3 * sizeof(float));
			return;
		}
		if (g_Config.bKeyhole)
		{
			static float keyhole_center = 0;
			float keyhole_snap = 0;

			if (g_Config.bKeyholeSnap)
				keyhole_snap = DEGREES_TO_RADIANS(g_Config.fKeyholeSnapSize);

			float keyhole_width = DEGREES_TO_RADIANS(g_Config.fKeyholeWidth / 2);
			float keyhole_left_bound = keyhole_center + keyhole_width;
			float keyhole_right_bound = keyhole_center - keyhole_width;

			// Correct left and right bounds if they calculated incorrectly and are out of the range of -PI to PI.
			if (keyhole_left_bound > (float)(M_PI))
				keyhole_left_bound -= (2 * (float)(M_PI));
			else if (keyhole_right_bound < -(float)(M_PI))
				keyhole_right_bound += (2 * (float)(M_PI));

			// Crossing from positive to negative half, counter-clockwise
			if (yaw < 0 && keyhole_left_bound > 0 && keyhole_right_bound > 0 && yaw < keyhole_width - (float)(M_PI))
			{
				keyhole_center = yaw - keyhole_width + keyhole_snap;
			}
			// Crossing from negative to positive half, clockwise
			else if (yaw > 0 && keyhole_left_bound < 0 && keyhole_right_bound < 0 && yaw >(float)(M_PI)-keyhole_width)
			{
				keyhole_center = yaw + keyhole_width - keyhole_snap;
			}
			// Already within the negative and positive range
			else if (keyhole_left_bound < 0 && keyhole_right_bound > 0)
			{
				if (yaw < keyhole_right_bound && yaw > 0)
					keyhole_center = yaw + keyhole_width - keyhole_snap;
				else if (yaw > keyhole_left_bound && yaw < 0)
					keyhole_center = yaw - keyhole_width + keyhole_snap;
			}
			// Anywhere within the normal range
			else
			{
				if (yaw < keyhole_right_bound)
					keyhole_center = yaw + keyhole_width - keyhole_snap;
				else if (yaw > keyhole_left_bound)
					keyhole_center = yaw - keyhole_width + keyhole_snap;
			}

			yaw -= keyhole_center;
		} 

		if (g_Config.bStabilizeYaw) {
			Matrix3x3 matrix_yaw;
			matrix_yaw.setRotationY(yaw);
			if (g_Config.bStabilizePitch && g_Config.bStabilizeRoll) {
				Matrix3x3 matrix_roll, matrix_pitch;
				matrix_roll.setRotationZ(-roll);
				matrix_pitch.setRotationX(-pitch);
				g_game_camera_rotmat = matrix_roll * matrix_pitch * matrix_yaw;
			} else if (g_Config.bStabilizeRoll) {
				Matrix3x3 matrix_roll;
				matrix_roll.setRotationZ(-roll);
				g_game_camera_rotmat = matrix_roll * matrix_yaw;
			} else if (g_Config.bStabilizePitch) {
				Matrix3x3 matrix_pitch;
				matrix_pitch.setRotationX(-pitch);
				g_game_camera_rotmat = matrix_pitch * matrix_yaw;
			} else {
				g_game_camera_rotmat = matrix_yaw;
			}
		} else if (g_Config.bStabilizeRoll && g_Config.bStabilizePitch) {
			Matrix3x3 matrix_roll, matrix_pitch;
			matrix_roll.setRotationZ(-roll);
			matrix_pitch.setRotationX(-pitch);
			g_game_camera_rotmat = matrix_roll * matrix_pitch;
		} else if (g_Config.bStabilizeRoll) {
			g_game_camera_rotmat.setRotationZ(-roll);
		} else if (g_Config.bStabilizePitch) {
			g_game_camera_rotmat.setRotationX(-pitch);
		}
		memset(g_game_camera_pos, 0, 3 * sizeof(float));
	}
	else
	{
		Matrix44::LoadIdentity(g_game_camera_rotmat);
		memset(g_game_camera_pos, 0, 3 * sizeof(float));
	}
}



ShaderManager::ShaderManager()
		: lastShader_(nullptr), globalDirty_(0xFFFFFFFF), shaderSwitchDirty_(0) {
	codeBuffer_ = new char[16384];
	lastFSID_.set_invalid();
	lastGSID_.set_invalid();
	lastVSID_.set_invalid();
}

ShaderManager::~ShaderManager() {
	delete [] codeBuffer_;
}

void ShaderManager::Clear() {
	DirtyLastShader();
	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		delete iter->ls;
	}
	for (auto iter = fsCache_.begin(); iter != fsCache_.end(); ++iter)	{
		delete iter->second;
	}
	for (auto iter = gsCache_.begin(); iter != gsCache_.end(); ++iter)	{
		delete iter->second;
	}
	for (auto iter = vsCache_.begin(); iter != vsCache_.end(); ++iter)	{
		delete iter->second;
	}
	linkedShaderCache_.clear();
	fsCache_.clear();
	gsCache_.clear();
	vsCache_.clear();
	globalDirty_ = 0xFFFFFFFF;
	lastFSID_.set_invalid();
	lastGSID_.set_invalid();
	lastVSID_.set_invalid();
	DirtyShader();
	bFrameChanged = true;
}

void ShaderManager::ClearCache(bool deleteThem) {
	Clear();
}

void ShaderManager::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.set_invalid();
	lastGSID_.set_invalid();
	lastVSID_.set_invalid();
	DirtyLastShader();
	globalDirty_ = 0xFFFFFFFF;
	shaderSwitchDirty_ = 0;
}

void ShaderManager::DirtyLastShader() { // disables vertex arrays
	if (lastShader_)
		lastShader_->stop();
	lastShader_ = nullptr;
	lastVShaderSame_ = false;
	lastGShaderSame_ = false;
}

// This is to be used when debugging why incompatible shaders are being linked, like is
// happening as I write this in Tactics Ogre
bool ShaderManager::DebugAreShadersCompatibleForLinking(Shader *vs, Shader *gs, Shader *fs) {
	// ShaderID vsid = vs->ID();
	// ShaderID gsid = gs->ID();
	// ShaderID fsid = fs->ID();
	// TODO: Redo these checks.
	return true;
}

Shader *ShaderManager::ApplyVertexShader(int prim, u32 vertType) {
	if (globalDirty_) {
		if (lastShader_)
			lastShader_->dirtyUniforms |= globalDirty_;
		shaderSwitchDirty_ |= globalDirty_;
		globalDirty_ = 0;
	}

	bool useHWTransform = CanUseHardwareTransform(prim);

	ShaderID VSID;
	ComputeVertexShaderID(&VSID, vertType, useHWTransform);

	// Just update uniforms if this is the same shader as last time.
	if (lastShader_ != 0 && VSID == lastVSID_) {
		lastVShaderSame_ = true;
		return lastShader_->vs_;  	// Already all set.
	} else {
		lastVShaderSame_ = false;
	}

	lastVSID_ = VSID;

	VSCache::iterator vsIter = vsCache_.find(VSID);
	Shader *vs;
	if (vsIter == vsCache_.end())	{
		// Vertex shader not in cache. Let's compile it.
		GenerateVertexShader(VSID, codeBuffer_);
		vs = new Shader(codeBuffer_, GL_VERTEX_SHADER, useHWTransform, VSID);

		if (vs->Failed()) {
			I18NCategory *gr = GetI18NCategory("Graphics");
			ERROR_LOG(G3D, "Shader compilation failed, falling back to software transform");
			osm.Show(gr->T("hardware transform error - falling back to software"), 2.5f, 0xFF3030FF, -1, true);
			delete vs;

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			ShaderID vsidTemp;
			ComputeVertexShaderID(&vsidTemp, vertType, false);
			GenerateVertexShader(vsidTemp, codeBuffer_);
			vs = new Shader(codeBuffer_, GL_VERTEX_SHADER, false, VSID);
		}

		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	return vs;
}

Shader *ShaderManager::ApplyGeometryShader(int prim, u32 vertType) {
	bool useHWTransform = CanUseHardwareTransform(prim);

	ShaderID GSID;
	ComputeGeometryShaderID(&GSID, prim);

	// Just update uniforms if this is the same shader as last time.
	if (lastShader_ != 0 && GSID == lastGSID_) {
		lastGShaderSame_ = true;
		return lastShader_->gs_;  	// Already all set.
	}
	else {
		lastGShaderSame_ = false;
	}

	lastGSID_ = GSID;

	GSCache::iterator gsIter = gsCache_.find(GSID);
	Shader *gs;
	if (gsIter == gsCache_.end())	{
		// Geometry shader not in cache. Let's compile it.
		GenerateGeometryShader(prim, codeBuffer_, useHWTransform);
		gs = new Shader(codeBuffer_, GL_GEOMETRY_SHADER, useHWTransform, GSID);

		if (gs->Failed()) {
			I18NCategory *gr = GetI18NCategory("Graphics");
			ERROR_LOG(G3D, "Geometry Shader compilation failed, falling back to software transform");
			osm.Show(gr->T("Geometry hardware transform error - falling back to software"), 2.5f, 0xFF3030FF, -1, true);
			delete gs;

			// TODO: Look for existing shader with the appropriate ID, use that instead of generating a new one - however, need to make sure
			// that that shader ID is not used when computing the linked shader ID below, because then IDs won't match
			// next time and we'll do this over and over...

			// Can still work with software transform.
			GenerateGeometryShader(prim, codeBuffer_, false);
			gs = new Shader(codeBuffer_, GL_GEOMETRY_SHADER, false, GSID);
		}

		gsCache_[GSID] = gs;
	}
	else {
		gs = gsIter->second;
	}
	return gs;
}

// This is the only place where UpdateUniforms is called!
LinkedShader *ShaderManager::ApplyFragmentShader(Shader *vs, Shader *gs, int prim, u32 vertType, bool isClear) {
	ShaderID FSID;
	ComputeFragmentShaderID(&FSID, vertType);
	if (lastVShaderSame_ && lastGShaderSame_ && FSID == lastFSID_) {
		u32 stillDirty = lastShader_->UpdateUniforms(vertType, isClear);
		globalDirty_ |= stillDirty;
		shaderSwitchDirty_ |= stillDirty;
		return lastShader_;
	}

	lastFSID_ = FSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	Shader *fs;
	if (fsIter == fsCache_.end())	{
		// Fragment shader not in cache. Let's compile it.
		if (!GenerateFragmentShader(FSID, codeBuffer_)) {
			return nullptr;
		}
		fs = new Shader(codeBuffer_, GL_FRAGMENT_SHADER, vs->UseHWTransform(), FSID);
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	// Okay, we have both shaders. Let's see if there's a linked one.
	LinkedShader *ls = nullptr;

	u32 switchDirty = shaderSwitchDirty_;
	for (auto iter = linkedShaderCache_.begin(); iter != linkedShaderCache_.end(); ++iter) {
		// Deferred dirtying! Let's see if we can make this even more clever later.
		iter->ls->dirtyUniforms |= switchDirty;

		if (iter->vs == vs && iter->gs == gs && iter->fs == fs) {
			ls = iter->ls;
		}
	}
	shaderSwitchDirty_ = 0;

	if (ls == nullptr) {
		// Check if we can link these.
#ifdef _DEBUG
		if (!DebugAreShadersCompatibleForLinking(vs, gs, fs)) {
			return nullptr;
		}
#endif
		ls = new LinkedShader(vs, gs, fs, vertType, vs->UseHWTransform(), lastShader_, isClear);  // This does "use" automatically
		const LinkedShaderCacheEntry entry(vs, gs, fs, ls);
		linkedShaderCache_.push_back(entry);
	} else {
		u32 stillDirty = ls->use(vertType, lastShader_, isClear);
		shaderSwitchDirty_ |= stillDirty;
		globalDirty_ |= stillDirty;
	}

	lastShader_ = ls;
	return ls;
}

std::string Shader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return isFragment_ ? FragmentShaderDesc(id_) : VertexShaderDesc(id_);
	default:
		return "N/A";
	}
}

std::vector<std::string> ShaderManager::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
		{
			for (auto iter : vsCache_) {
				iter.first.ToString(&id);
				ids.push_back(id);
			}
		}
		break;
	case SHADER_TYPE_FRAGMENT:
		{
			for (auto iter : fsCache_) {
				iter.first.ToString(&id);
				ids.push_back(id);
			}
		}
		break;
	default:
		break;
	}
	return ids;
}

std::string ShaderManager::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		auto iter = vsCache_.find(shaderId);
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}

	case SHADER_TYPE_FRAGMENT:
	{
		auto iter = fsCache_.find(shaderId);
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}
	default:
		return "N/A";
	}
}
