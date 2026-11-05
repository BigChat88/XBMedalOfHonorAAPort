/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// skeletor_animation_file_format.h : Skeletor animation file format

#pragma once

typedef float skelAnimChannel_t[4];

typedef struct {
    SkelVec3 bounds[2];
    float    radius;
    SkelVec3 delta;
    float    angleDelta;
    int      iOfsChannels;
} skelAnimFileFrame_t;

typedef struct {
    int                 ident;
    int                 version;
    int                 flags;
    int                 nBytesUsed;
    float               frameTime;
    SkelVec3            totalDelta;
    float               totalAngleDelta;
    int                 numChannels;
    int                 ofsChannelNames;
    int                 numFrames;
    skelAnimFileFrame_t frame[1];
} skelAnimDataFileHeader_t;

typedef struct {
    SkelVec3 bounds[2];
    float    radius;
    SkelVec3 delta;
    float    angleDelta;
    vec4_t  *pChannels; // FIXME: must delete
} skelAnimGameFrame_t;

typedef struct {
    short int nFrameNum;
    short int nPrevFrameIndex;
    float     pChannelData[1];
} skanGameFrame;

typedef struct {
    short int      nFramesInChannel;
    skanGameFrame *ary_frames;
} skanChannelHdr;

#ifdef NXDK
// OGX MOHAA: once loaded, an animation's channels are packed into one block
// (Skeletor_XboxPackAnim) and their frames into 16-bit fixed point:
// - rotations keep x, y, z of the quaternion made to have w >= 0 (q and -q
//   are the same rotation, and the blending in GetSlerpValue already flips
//   signs), w is rebuilt when decoding (20 -> 10 bytes)
// - positions are scaled per channel: the scale is a float stored right before
//   the channel's first frame (16 -> 10 bytes)
typedef struct {
    short int nFrameNum;
    short int nPrevFrameIndex;
    short int q[3]; // quaternion x, y, z * 32767 (w >= 0)
} skanGameFrameRotPacked;

typedef struct {
    short int nFrameNum;
    short int nPrevFrameIndex;
    short int p[3]; // position / scale
} skanGameFramePosPacked;
#endif

typedef struct {
    short  nFrameNum;
    vec3_t pos;
    vec3_t rot;
} skanAnimFrame;

typedef struct skelAnimDataGameHeader_s skelAnimDataGameHeader_t;

#ifdef __cplusplus

typedef struct skelAnimDataGameHeader_s {
    int                  flags;
    size_t               nBytesUsed;
    bool                 bHasDelta;
    bool                 bHasMorph;
    bool                 bHasUpper;
    int                  numFrames;
    SkelVec3             totalDelta;
    float                totalAngleDelta;
    float                frameTime;
    skelChannelList_c    channelList;
    SkelVec3             bounds[2];
    skelAnimGameFrame_t *m_frame;
    short int            nTotalChannels;
    skanChannelHdr       ary_channels[1];

    skelAnimDataGameHeader_s(const skelAnimDataGameHeader_t&);
    skelAnimDataGameHeader_s();

    static skelAnimDataGameHeader_t *AllocRLEChannelData(size_t numChannels);
    int                              GetFrameNums(
                                     float  timeSeconds,
                                     float  timeTolerance,
                                     int   *beforeFrame,
                                     int   *afterFrame,
                                     float *beforeWeight,
                                     float *afterWeight
                                 );
    SkelVec3                         GetDeltaOverTime(float time1, float time2);
    float                            GetAngularDeltaOverTime(float time1, float time2);
    static skelAnimDataGameHeader_t *AllocAnimData(size_t numFrames, size_t numChannels);
    static void                      DeallocAnimData(skelAnimDataGameHeader_t *data);
} skelAnimDataGameHeader_t;

#ifdef NXDK
void Skeletor_XboxPackAnim(skelAnimDataGameHeader_t *anim); // skeletor_loadanimation.cpp
#endif

#endif
