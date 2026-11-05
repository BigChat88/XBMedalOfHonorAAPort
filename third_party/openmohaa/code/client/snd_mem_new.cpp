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

#include "snd_local.h"
#ifdef NXDK
#    include "../../../../xbox/mohaa/src/xbox_ima4.h"
#    include "snd_codec.h"
#endif
#include "cl_ui.h"

byte *data_p;
byte *iff_end;
byte *last_chunk;
byte *iff_data;
int   iff_chunk_len;

qboolean S_LoadMP3(const char *fileName, sfx_t *sfx);

/*
==============
GetLittleShort
==============
*/
short int GetLittleShort()
{
    union {
        short value;
        byte  bytes[2];
    } val;

#ifdef Q3_LITTLE_ENDIAN
    val.bytes[0] = data_p[0];
    val.bytes[1] = data_p[1];
#else
    val.bytes[0] = data_p[1];
    val.bytes[1] = data_p[0];
#endif

    data_p += sizeof(short);
    return val.value;
}

/*
==============
GetLittleLong
==============
*/
int GetLittleLong()
{
    union {
        int  value;
        byte bytes[4];
    } val;

#ifdef Q3_LITTLE_ENDIAN
    val.bytes[0] = data_p[0];
    val.bytes[1] = data_p[1];
    val.bytes[2] = data_p[2];
    val.bytes[3] = data_p[3];
#else
    val.bytes[0] = data_p[3];
    val.bytes[1] = data_p[2];
    val.bytes[2] = data_p[1];
    val.bytes[3] = data_p[0];
#endif

    data_p += sizeof(int);
    return val.value;
}

/*
==============
SetLittleShort
==============
*/
void SetLittleShort(int i)
{
    union {
        short value;
        byte  bytes[2];
    } val;

    val.value = i;

#ifdef Q3_LITTLE_ENDIAN
    data_p[0] = val.bytes[0];
    data_p[1] = val.bytes[1];
#else
    data_p[0] = val.bytes[1];
    data_p[1] = val.bytes[0];
#endif

    data_p += sizeof(short);
}

/*
==============
SetLittleLong
==============
*/
void SetLittleLong(int i)
{
    union {
        int  value;
        byte bytes[4];
    } val;

    val.value = i;

#ifdef Q3_LITTLE_ENDIAN
    data_p[0] = val.bytes[0];
    data_p[1] = val.bytes[1];
    data_p[2] = val.bytes[2];
    data_p[3] = val.bytes[3];
#else
    data_p[0] = val.bytes[3];
    data_p[1] = val.bytes[2];
    data_p[2] = val.bytes[1];
    data_p[3] = val.bytes[0];
#endif

    data_p += sizeof(int);
}

/*
==============
FindNextChunk
==============
*/
void FindNextChunk(const char *name)
{
    int value;

    while (1) {
        data_p = last_chunk;

        if (last_chunk >= (byte *)iff_end) {
            break;
        }

        data_p = last_chunk + 4;
        value  = GetLittleLong();

        iff_chunk_len = value;
        if (value < 0) {
            break;
        }

        data_p -= 8;
        value++;
        value &= ~1;

        last_chunk = data_p + value + 8;
        if (!strncmp((const char *)data_p, name, 4u)) {
            return;
        }
    }

    data_p = NULL;
}

/*
==============
FindChunk
==============
*/
void FindChunk(const char *name)
{
    last_chunk = iff_data;
    FindNextChunk(name);
}

/*
==============
DumpChunks
==============
*/
void DumpChunks()
{
    char str[5];

    str[4] = 0;

    data_p = iff_data;
    do {
        memcpy(str, data_p, 4);
        data_p += 4;

        iff_chunk_len = GetLittleLong();

        Com_Printf("0x%p : %s (%d)\n", data_p - 4, str, iff_chunk_len);

        data_p += (iff_chunk_len + 1) & ~1;
    } while (data_p < (byte *)iff_end);
}

/*
==============
GetWavinfo
==============
*/
wavinfo_t GetWavinfo(const char *name, byte *wav, int wavlength)
{
    wavinfo_t info;
    int       samples;
    short     bytealign;

    memset(&info, 0, sizeof(wavinfo_t));

    if (!wav) {
        return info;
    }

    iff_data = wav;
    iff_end  = &wav[wavlength];
    FindChunk("RIFF");

    if (!data_p || strncmp((const char *)data_p + 8, "WAVE", 4u)) {
        Com_Printf("Missing RIFF/WAVE chunks\n");
        return info;
    }

    iff_data = data_p + 12;
    FindChunk("fmt ");
    if (!data_p) {
        Com_Printf("Missing fmt chunk\n");
        return info;
    }

    data_p += 8;

    info.format = GetLittleShort();
    if (info.format == 17) {
        info.channels = GetLittleShort();
        info.rate     = (float)GetLittleLong();
        data_p += 4;

        bytealign  = GetLittleShort();
        info.width = (float)GetLittleShort() / 8.f;
        data_p += 2;

        FindChunk("data");
        if (!data_p) {
            Com_Printf("Missing data chunk\n");
            return info;
        }

        data_p += 4;
        if (info.width >= 1.0) {
            samples = (int)((float)GetLittleLong() / info.width);
        } else {
            samples = (int)((float)GetLittleLong() * info.width);
        }

        if (!info.samples) {
            info.samples = samples;
        } else if (samples < info.samples) {
            Com_Error(ERR_DROP, "Sound %s has a bad loop length", name);
        }

        info.dataofs   = data_p - wav;
        info.datasize  = iff_chunk_len - bytealign + info.dataofs;
        info.dataalign = (bytealign / info.channels - 4) / 4 * 8 + 1;
    } else if (info.format == 1) {
        info.channels = GetLittleShort();
        info.rate     = (float)GetLittleLong();
        data_p += 4;

        bytealign  = GetLittleShort();
        info.width = (float)GetLittleShort() / 8.f;

        FindChunk("data");
        if (!data_p) {
            Com_Printf("Missing data chunk\n");
            return info;
        }

        data_p += 4;
        samples = (float)GetLittleLong() / info.width;

        if (!info.samples) {
            info.samples = samples;
        } else if (samples < info.samples) {
            Com_Error(ERR_DROP, "Sound %s has a bad loop length", name);
        }

        info.dataofs   = data_p - wav;
        info.datasize  = iff_chunk_len;
        info.dataalign = (bytealign / info.channels - 4) / 4 * 8 + 1;
        // dataalign should always be 1
        assert(info.dataalign == 1);
    } else {
        Com_Printf("Microsoft PCM format only\n");
        return info;
    }

    return info;
}

/*
==============
DownSampleWav
==============
*/
qboolean DownSampleWav(wavinfo_t *info, byte *wav, int wavlength, int newkhz, byte **newdata)
{
    int   newdatasize;
    byte *datap;
    int   i;
    int   ii;
    int   error;
    int   width;
    int   oldsamples;
    int   oldrate;

    newdatasize = 0;
    datap       = &wav[info->dataofs];

    if (info->channels > 1) {
        Com_DPrintf("Could not downsample WAV file. Stereo WAVs not supported!\n");
        return 0;
    }

    if (info->format != 1 || !info->dataofs) {
        Com_DPrintf("Could not downsample WAV file. Not PCM format!\n");
        return 0;
    }

    if (info->rate <= newkhz) {
        return 0;
    }

    error = 0;
    width = info->width;
    for (i = 0; i < info->samples; i++) {
        error += newkhz;
        while (error > info->rate) {
            error -= info->rate;
            newdatasize += width;
        }
    }

    oldsamples    = info->samples;
    oldrate       = info->rate;
    info->samples = newdatasize / width;
    info->rate    = (float)newkhz;
    newdatasize += info->dataofs;

    *newdata = (byte *)Z_TagMalloc(newdatasize, TAG_SOUND);
    memcpy(*newdata, wav, info->dataofs);

    iff_data = *newdata;
    iff_end  = *newdata + newdatasize;
    FindChunk("RIFF");

    if (!data_p || strncmp((const char *)data_p + 8, "WAVE", 4u)) {
        Com_DPrintf("Missing RIFF/WAVE chunks\n");
        return 0;
    }

    iff_data = data_p + 12;
    FindChunk("fmt ");
    if (!data_p) {
        Com_DPrintf("Missing fmt chunk\n");
        return 0;
    }

    data_p += 12;
    SetLittleShort((int)info->rate);
    data_p += 8;

    FindChunk("data");
    if (!data_p) {
        Com_DPrintf("Missing data chunk\n");
        return 0;
    }

    data_p += 4;
    SetLittleLong((int)(info->samples * info->width));

    error = 0;
    for (i = 0; i < oldsamples; i++) {
        error += newkhz;
        while (error > oldrate) {
            error -= oldrate;
            for (ii = 0; ii < width; ii++) {
                data_p[ii] = datap[ii];
            }

            data_p += width;
        }

        datap += width;
    }

    assert(iff_end - data_p == 0);

    return newdatasize;
}

/*
==============
DownSampleWav_MILES
==============
*/
int DownSampleWav_MILES(wavinfo_t *info, byte *wav, int wavlength, int newkhz, byte **newdata)
{
    // Fallback to software downsampling
    return DownSampleWav(info, wav, wavlength, newkhz, newdata);
}

/*
==============
S_LoadSound
==============
*/
qboolean S_LoadSound(const char *fileName, sfx_t *sfx, int streamed, qboolean force_load)
{
    int          size;
    fileHandle_t file_handle;
    char         tempName[MAX_RES_NAME + 1];
    int          realKhz;

    sfx->buffer = 0;

    if (fileName[0] == '*') {
        return qfalse;
    }

    if (streamed) {
        sfx->length = 5000;
        sfx->width  = 1;
        sfx->iFlags |= SFX_FLAG_STREAMED;
        sfx->time_length = 5000.0;
        sfx->data        = NULL;
        return qtrue;
    }

    if (clc.state != CA_LOADING && !force_load) {
        Com_Printf("**************S_LoadSound: %s\n", fileName);
    }

    if (strstr(fileName, ".mp3")) {
        return S_LoadMP3(fileName, sfx);
    }

    size = FS_FOpenFileRead(fileName, &file_handle, qfalse, qtrue);
    if (size <= 0) {
        if (file_handle) {
            FS_FCloseFile(file_handle);
        }
        return qfalse;
    }

    sfx->data = (byte *)Z_TagMalloc(size, TAG_SOUND);

    FS_Read(sfx->data, size, file_handle);
    FS_FCloseFile(file_handle);
    sfx->info = GetWavinfo(fileName, sfx->data, size);

    if (sfx->info.channels != 1 && !streamed) {
        Com_Printf("%s is a stereo wav file\n", fileName);
        Z_Free(sfx->data);
        sfx->data = NULL;
        return qfalse;
    }

    if (!sfx->info.dataofs) {
        sfx->iFlags |= SFX_FLAG_NO_OFFSET;
    }

    switch (s_khz->integer) {
    case 11:
        realKhz = 11025;
        break;
    case 22:
    default:
        realKhz = 22050;
        break;
    case 44:
#ifdef NXDK
        // OGX MOHAA: sounds stay in RAM as PCM; 44 kHz would not fit
        realKhz = 22050;
#else
        realKhz = 44100;
#endif
        break;
    }

    if (!(sfx->iFlags & SFX_FLAG_STREAMED) && realKhz < sfx->info.rate) {
        byte *newdata;
        int   newdatasize;

        newdata = NULL;
        if (sfx->iFlags & SFX_FLAG_NO_OFFSET) {
            newdatasize = DownSampleWav_MILES(&sfx->info, sfx->data, size, realKhz, &newdata);
        } else {
            newdatasize = DownSampleWav(&sfx->info, sfx->data, size, realKhz, &newdata);
        }

        if (newdatasize) {
            Z_Free(sfx->data);
            sfx->data          = newdata;
            sfx->info.datasize = newdatasize;
        }
    }

#ifdef NXDK
    // OGX MOHAA: keep 16-bit mono sounds as IMA ADPCM (4:1). The result looks
    // like an IMA ADPCM WAV (format 17, 4 bits, dataalign = samples per block)
    // without a header; the OpenAL mixer decodes it.
    if (sfx->data && !(sfx->iFlags & SFX_FLAG_STREAMED) && sfx->info.format == 1 && sfx->info.width == 2
        && sfx->info.channels == 1 && sfx->info.samples > 0 && sfx->info.dataofs) {
        const int imaSize = Xbox_IMA4_EncodedSize(sfx->info.samples);
        byte     *ima     = (byte *)Z_TagMalloc(imaSize, TAG_SOUND);

        Xbox_IMA4_Encode((const short *)(sfx->data + sfx->info.dataofs), sfx->info.samples, ima);
        Z_Free(sfx->data);
        sfx->data            = ima;
        sfx->info.format     = 17;
        sfx->info.width      = 0.5f;
        sfx->info.dataofs    = 0;
        sfx->info.datasize   = imaSize;
        sfx->info.dataalign  = XBOX_IMA4_BLOCK_SAMPLES;
    }
#endif

    sfx->length      = sfx->info.samples;
    sfx->width       = sfx->info.width;
    sfx->time_length = sfx->info.samples / sfx->info.rate * 1000.f;

    if (sfx->iFlags & SFX_FLAG_STREAMED) {
        Z_Free(sfx->data);
        sfx->data = NULL;
    }

    Com_sprintf(tempName, sizeof(tempName), "k%s", fileName);
    UI_LoadResource(tempName);

    if (strstr(fileName, "sound/null.wav")) {
        sfx->iFlags |= SFX_FLAG_NULL;
        return qtrue;
    }

    return qtrue;
}

/*
==============
S_LoadMP3
==============
*/
#ifdef NXDK
/*
==============
S_Xbox_LoadMP3AsIMA4

OGX MOHAA: MP3 sounds that aren't streamed (dialogue) used to be decoded whole
to PCM when played (temp memory, then a copy in the AL buffer kept for the
level: several MB for a few lines). They are decoded here in small pieces,
mixed to mono, brought down to 22 kHz at most and kept as IMA ADPCM, which is
smaller than the MP3 itself; from then on they are ordinary sounds.
==============
*/
static qboolean S_Xbox_LoadMP3AsIMA4(const char *fileName, sfx_t *sfx)
{
    snd_stream_t *stream;
    short         in[2048];
    short         block[XBOX_IMA4_BLOCK_SAMPLES];
    byte         *ima;
    int           channels, step, maxBlocks;
    int           blocks = 0, blockFill = 0, total = 0;
    int           pending = 0, havePending = 0;

    stream = S_CodecOpenStream(fileName);
    if (!stream) {
        return qfalse;
    }

    channels = stream->info.channels;
    if (stream->info.width != 2 || channels < 1 || channels > 2 || stream->info.samples <= 0
        || stream->info.rate <= 0) {
        S_CodecCloseStream(stream);
        return qfalse;
    }

    step      = stream->info.rate > 22050 ? 2 : 1;
    maxBlocks = (stream->info.samples / step + XBOX_IMA4_BLOCK_SAMPLES) / XBOX_IMA4_BLOCK_SAMPLES;
    ima       = (byte *)Z_TagMalloc(maxBlocks * XBOX_IMA4_BLOCK_BYTES, TAG_SOUND);

    while (blocks < maxBlocks) {
        const int bytes  = S_CodecReadStream(stream, sizeof(in), in);
        const int frames = bytes / (2 * channels);

        if (frames <= 0) {
            break;
        }

        for (int i = 0; i < frames && blocks < maxBlocks; i++) {
            int s = channels == 2 ? (in[i * 2] + in[i * 2 + 1]) / 2 : in[i];

            if (step == 2) {
                // average pairs: a cheap low-pass for the decimation
                if (!havePending) {
                    pending     = s;
                    havePending = 1;
                    continue;
                }
                s           = (pending + s) / 2;
                havePending = 0;
            }

            block[blockFill++] = (short)s;
            if (blockFill == XBOX_IMA4_BLOCK_SAMPLES) {
                Xbox_IMA4_Encode(block, blockFill, ima + blocks * XBOX_IMA4_BLOCK_BYTES);
                blocks++;
                total += blockFill;
                blockFill = 0;
            }
        }
    }

    if (blockFill && blocks < maxBlocks) {
        Xbox_IMA4_Encode(block, blockFill, ima + blocks * XBOX_IMA4_BLOCK_BYTES);
        blocks++;
        total += blockFill;
    }

    sfx->info.rate = (float)(stream->info.rate / step);
    S_CodecCloseStream(stream);

    if (!total) {
        Z_Free(ima);
        return qfalse;
    }

    sfx->data           = ima;
    sfx->info.format    = 17;
    sfx->info.width     = 0.5f;
    sfx->info.channels  = 1;
    sfx->info.samples   = total;
    sfx->info.dataofs   = 0;
    sfx->info.datasize  = blocks * XBOX_IMA4_BLOCK_BYTES;
    sfx->info.dataalign = XBOX_IMA4_BLOCK_SAMPLES;
    sfx->length         = total;
    sfx->width          = sfx->info.width;
    sfx->time_length    = total / sfx->info.rate * 1000.f;

    return qtrue;
}
#endif

qboolean S_LoadMP3(const char *fileName, sfx_t *sfx)
{
    int          length;
    fileHandle_t file_handle;

#ifdef NXDK
    memset(&sfx->info, 0, sizeof(sfx->info));
    if (S_Xbox_LoadMP3AsIMA4(fileName, sfx)) {
        return qtrue;
    }
#endif

    length = FS_FOpenFileRead(fileName, &file_handle, 0, 1);
    if (length <= 0) {
        if (file_handle) {
            FS_FCloseFile(file_handle);
        }
        return qfalse;
    }

    memset(&sfx->info, 0, sizeof(sfx->info));
    sfx->data   = (byte *)Z_TagMalloc(length, TAG_SOUND);
    sfx->length = length;
    sfx->width  = 1;
    FS_Read(sfx->data, length, file_handle);
    FS_FCloseFile(file_handle);

    sfx->iFlags |= SFX_FLAG_MP3;

    return qtrue;
}
