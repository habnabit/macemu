/*
 *  video.h - Video/graphics emulation
 *
 *  SheepShaver (C) 1997-2008 Marc Hellwig and Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef VIDEO_H
#define VIDEO_H

extern bool VideoActivated(void);
extern bool VideoSnapshot(int xsize, int ysize, uint8 *p);

extern int16 VideoDoDriverIO(uint32 spaceID, uint32 commandID, uint32 commandContents, uint32 commandCode, uint32 commandKind);

// System specific and internal functions/data
struct VideoInfo {
	int	viType;				// Screen/Window
	uint32 viRowBytes;		// width of each row in memory
	uint16 viXsize,viYsize;	// Window
	uint32 viAppleMode;		// Screen Color Depth
	uint32 viAppleID;		// Screen DisplayID
};

enum {	// viAppleMode
	APPLE_1_BIT = 0x80,
	APPLE_2_BIT,
	APPLE_4_BIT,
	APPLE_8_BIT,
	APPLE_16_BIT,
	APPLE_32_BIT
};

// 1, 2, 4 and 8 bit depths use a color palette
inline bool IsDirectMode(int mode)
{
	return mode == APPLE_16_BIT || mode == APPLE_32_BIT;
}

// Return the depth code that corresponds to the specified bits-per-pixel value
inline int DepthModeForPixelDepth(int depth)
{
	switch (depth) {
	case 1: return APPLE_1_BIT;
	case 2: return APPLE_2_BIT;
	case 4: return APPLE_4_BIT;
	case 8: return APPLE_8_BIT;
	case 15: case 16: return APPLE_16_BIT;
	case 24: case 32: return APPLE_32_BIT;
	default: return APPLE_1_BIT;
	}
}

// Return a bytes-per-row value (assuming no padding) for the specified depth and pixel width
inline uint32 TrivialBytesPerRow(uint32 width, int mode)
{
	switch (mode) {
	case APPLE_1_BIT: return width / 8;
	case APPLE_2_BIT: return width / 4;
	case APPLE_4_BIT: return width / 2;
	case APPLE_8_BIT: return width;
	case APPLE_16_BIT: return width * 2;
	case APPLE_32_BIT: return width * 4;
	default: return width;
	}
}

enum {	// viAppleID
	APPLE_640x480 = 0x81,
	APPLE_W_640x480,
	APPLE_800x600,
	APPLE_W_800x600,
	APPLE_1024x768,
	APPLE_1152x768,
	APPLE_1152x900,
	APPLE_1280x1024,
	APPLE_1600x1200,
	APPLE_CUSTOM,
	APPLE_ID_MIN = APPLE_640x480,
	APPLE_ID_MAX = APPLE_CUSTOM
};

enum {	// Display type
	DIS_INVALID,
	DIS_SCREEN,
	DIS_WINDOW
};

struct VidLocals{
	uint16	saveMode;
	uint32	saveData;
	uint16	savePage;
	uint32	saveBaseAddr;
	uint32	gammaTable;			// Mac address of gamma tble
	uint32	maxGammaTableSize;	// Biggest gamma table allocated
	uint32	saveVidParms;
	bool	luminanceMapping;	// Luminance mapping on/off
	bool	cursorHardware;		// True if using hardware cursor
	int32	cursorX;			// Hardware cursor state
	int32	cursorY;
	uint32	cursorVisible;
	uint32	cursorSet;
	bool	cursorHotFlag;
	uint8	cursorHotX;
	uint8	cursorHotY;
	uint32	vslServiceID;		// VSL interrupt service ID
	bool	interruptsEnabled;	// VBL interrupts on/off
	uint32	regEntryID;			// Mac address of the service owner
};

extern bool VideoInit(void);
extern void VideoExit(void);
extern void VideoVBL(void);
extern void VideoInstallAccel(void);
extern void VideoQuitFullScreen(void);
extern void VideoSaveBuffer(void);

extern void video_set_palette(void);
extern void video_set_cursor(void);
extern bool video_can_change_cursor(void);
extern int16 video_mode_change(VidLocals *csSave, uint32 ParamPtr);
extern void video_set_dirty_area(int x, int y, int w, int h);

extern int16 VSLDoInterruptService(uint32 arg1);
extern void NQDMisc(uint32 arg1, uintptr arg2);

// Native QuickDraw acceleration callbacks
extern bool NQD_sync_hook(uint32);
extern bool NQD_bitblt_hook(uint32);
extern bool NQD_fillrect_hook(uint32);
extern bool NQD_unknown_hook(uint32);
extern void NQD_bitblt(uint32);
extern void NQD_invrect(uint32);
extern void NQD_fillrect(uint32);

class video_state_t
{
public:
	bool keyfile_valid;
	VidLocals private_data;	// Driver local variables
	bool video_activated;		// Flag: video display activated, mouse and keyboard data valid
	uint32 screen_base;				// Frame buffer base address
	int cur_mode;						// Number of current video mode (index in VModes array)
	int display_type;		// Current display type
	rgb_color mac_pal[256];
	uint8 remap_mac_be[256];
	uint8 MacCursor[68];	// Mac cursor image
	struct VideoInfo VModes[64];
	long save_conf_id;
	long save_conf_mode;

	video_state_t();
};

#endif
