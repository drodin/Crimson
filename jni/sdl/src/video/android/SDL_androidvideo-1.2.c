/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2009 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"

#include "SDL_androidvideo.h"

#include <jni.h>
#include <android/log.h>
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <string.h> // for memset()

#define _THIS	SDL_VideoDevice *this

/* Initialization/Query functions */
static int ANDROID_VideoInit(_THIS, SDL_PixelFormat *vformat);
static SDL_Rect **ANDROID_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *ANDROID_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int ANDROID_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors);
static void ANDROID_VideoQuit(_THIS);

/* Hardware surface functions */
static int ANDROID_AllocHWSurface(_THIS, SDL_Surface *surface);
static int ANDROID_LockHWSurface(_THIS, SDL_Surface *surface);
static void ANDROID_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void ANDROID_FreeHWSurface(_THIS, SDL_Surface *surface);
static int ANDROID_FlipHWSurface(_THIS, SDL_Surface *surface);
static void ANDROID_GL_SwapBuffers(_THIS);
static void ANDROID_PumpEvents(_THIS);

// Stubs to get rid of crashing in OpenGL mode
// The implementation dependent data for the window manager cursor
struct WMcursor {
    int unused ;
};

void ANDROID_FreeWMCursor(_THIS, WMcursor *cursor) {
    SDL_free (cursor);
    return;
}
WMcursor * ANDROID_CreateWMCursor(_THIS, Uint8 *data, Uint8 *mask, int w, int h, int hot_x, int hot_y) {
    WMcursor * cursor;
    cursor = (WMcursor *) SDL_malloc (sizeof (WMcursor)) ;
    if (cursor == NULL) {
        SDL_OutOfMemory () ;
        return NULL ;
    }
    return cursor;
}
int ANDROID_ShowWMCursor(_THIS, WMcursor *cursor) {
    return 1;
}
void ANDROID_WarpWMCursor(_THIS, Uint16 x, Uint16 y) { }
void ANDROID_MoveWMCursor(_THIS, int x, int y) { }


/* etc. */
static void ANDROID_UpdateRects(_THIS, int numrects, SDL_Rect *rects);


/* Private display data */

#define SDL_NUMMODES 4
struct SDL_PrivateVideoData {
	SDL_Rect *SDL_modelist[SDL_NUMMODES+1];
};

#define SDL_modelist		(this->hidden->SDL_modelist)


// Pointer to in-memory video surface
static int memX = 0;
static int memY = 0;
int SDL_ANDROID_sFakeWindowWidth = 320;
int SDL_ANDROID_sFakeWindowHeight = 480;
// In-memory surfaces
static uint16_t * part_screen = NULL;
static uint16_t * memBuffer1 = NULL;
static uint16_t * memBuffer = NULL;
static int sdl_opengl = 0;
// Some wicked GLES stuff
static GLuint texture = 0;


static void SdlGlRenderInit();


/* ANDROID driver bootstrap functions */

static int ANDROID_Available(void)
{
	return 1;
}

static void ANDROID_DeleteDevice(SDL_VideoDevice *device)
{
	SDL_free(device->hidden);
	SDL_free(device);
}

static SDL_VideoDevice *ANDROID_CreateDevice(int devindex)
{
	SDL_VideoDevice *device;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
	if ( device ) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
				SDL_malloc((sizeof *device->hidden));
	}
	if ( (device == NULL) || (device->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( device ) {
			SDL_free(device);
		}
		return(0);
	}
	SDL_memset(device->hidden, 0, (sizeof *device->hidden));

	/* Set the function pointers */
	device->VideoInit = ANDROID_VideoInit;
	device->ListModes = ANDROID_ListModes;
	device->SetVideoMode = ANDROID_SetVideoMode;
	device->CreateYUVOverlay = NULL;
	device->SetColors = ANDROID_SetColors;
	device->UpdateRects = ANDROID_UpdateRects;
	device->VideoQuit = ANDROID_VideoQuit;
	device->AllocHWSurface = ANDROID_AllocHWSurface;
	device->CheckHWBlit = NULL;
	device->FillHWRect = NULL;
	device->SetHWColorKey = NULL;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = ANDROID_LockHWSurface;
	device->UnlockHWSurface = ANDROID_UnlockHWSurface;
	device->FlipHWSurface = ANDROID_FlipHWSurface;
	device->FreeHWSurface = ANDROID_FreeHWSurface;
	device->SetCaption = NULL;
	device->SetIcon = NULL;
	device->IconifyWindow = NULL;
	device->GrabInput = NULL;
	device->GetWMInfo = NULL;
	device->InitOSKeymap = ANDROID_InitOSKeymap;
	device->PumpEvents = ANDROID_PumpEvents;
	device->GL_SwapBuffers = ANDROID_GL_SwapBuffers;
	device->free = ANDROID_DeleteDevice;

	// Stubs
	device->FreeWMCursor = ANDROID_FreeWMCursor;
	device->CreateWMCursor = ANDROID_CreateWMCursor;
	device->ShowWMCursor = ANDROID_ShowWMCursor;
	device->WarpWMCursor = ANDROID_WarpWMCursor;
	device->MoveWMCursor = ANDROID_MoveWMCursor;

	return device;
}

VideoBootStrap ANDROID_bootstrap = {
	"android", "SDL android video driver",
	ANDROID_Available, ANDROID_CreateDevice
};


int ANDROID_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	int i;
	/* Determine the screen depth (use default 16-bit depth) */
	/* we change this during the SDL_SetVideoMode implementation... */
	vformat->BitsPerPixel = 16;
	vformat->BytesPerPixel = 2;

	for ( i=0; i<SDL_NUMMODES; ++i ) {
		SDL_modelist[i] = SDL_malloc(sizeof(SDL_Rect));
		SDL_modelist[i]->x = SDL_modelist[i]->y = 0;
	}
	/* Modes sorted largest to smallest */
	SDL_modelist[0]->w = SDL_ANDROID_sWindowWidth; SDL_modelist[0]->h = SDL_ANDROID_sWindowHeight;
	SDL_modelist[1]->w = 640; SDL_modelist[1]->h = 480; // Will likely be shrinked
	SDL_modelist[2]->w = 320; SDL_modelist[2]->h = 240; // Always available on any screen and any orientation
	SDL_modelist[3]->w = 320; SDL_modelist[3]->h = 200; // Always available on any screen and any orientation
	SDL_modelist[4] = NULL;

	/* We're done! */
	return(0);
}

SDL_Rect **ANDROID_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
	if(format->BitsPerPixel != 16)
		return NULL;
	return SDL_modelist;
}

SDL_Surface *ANDROID_SetVideoMode(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{
    __android_log_print(ANDROID_LOG_INFO, "libSDL", "SDL_SetVideoMode(): application requested mode %dx%d", width, height);

	if ( memBuffer1 )
		SDL_free( memBuffer1 );

    if ( part_screen )
		SDL_free( part_screen );

	memBuffer = memBuffer1 = part_screen = NULL;

	sdl_opengl = (flags & SDL_OPENGL) ? 1 : 0;

	memX = width;
	memY = height;
	SDL_ANDROID_sFakeWindowWidth = width;
	SDL_ANDROID_sFakeWindowHeight = height;
	
	if( ! sdl_opengl )
	{
		memBuffer1 = SDL_malloc(memX * memY * (bpp / 8));
		if ( ! memBuffer1 ) {
			__android_log_print(ANDROID_LOG_INFO, "libSDL", "Couldn't allocate buffer for requested mode");
			SDL_SetError("Couldn't allocate buffer for requested mode");
			return(NULL);
		}
		SDL_memset(memBuffer1, 0, memX * memY * (bpp / 8));

        part_screen = SDL_malloc(memX * memY * (bpp / 8));
		if ( ! part_screen ) {
			__android_log_print(ANDROID_LOG_INFO, "libSDL", "Couldn't allocate buffer for requested mode");
			SDL_SetError("Couldn't allocate buffer for requested mode");
			return(NULL);
		}
		SDL_memset(part_screen, 0, memX * memY * (bpp / 8));

		memBuffer = memBuffer1;
	}

	/* Allocate the new pixel format for the screen */
	if ( ! SDL_ReallocFormat(current, bpp, 0, 0, 0, 0) ) {
		if(memBuffer)
			SDL_free(memBuffer);
		memBuffer = NULL;
		__android_log_print(ANDROID_LOG_INFO, "libSDL", "Couldn't allocate new pixel format for requested mode");
		SDL_SetError("Couldn't allocate new pixel format for requested mode");
		return(NULL);
	}

	/* Set up the new mode framebuffer */
	current->flags = (flags & SDL_FULLSCREEN) | (flags & SDL_OPENGL);
	current->w = width;
	current->h = height;
	current->pitch = memX * (bpp / 8);
	current->pixels = memBuffer;
	
	SdlGlRenderInit();

	/* We're done */
	return(current);
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void ANDROID_VideoQuit(_THIS)
{
	if( ! sdl_opengl )
	{
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDeleteTextures(1, &texture);
	}

	memX = 0;
	memY = 0;
	memBuffer = NULL;
	SDL_free( memBuffer1 );
	memBuffer1 = NULL;
    SDL_free( part_screen );
	part_screen = NULL;

	int i;
	
	if (this->screen->pixels != NULL)
	{
		SDL_free(this->screen->pixels);
		this->screen->pixels = NULL;
	}
	/* Free video mode lists */
	for ( i=0; i<SDL_NUMMODES; ++i ) {
		if ( SDL_modelist[i] != NULL ) {
			SDL_free(SDL_modelist[i]);
			SDL_modelist[i] = NULL;
		}
	}
}

void ANDROID_PumpEvents(_THIS)
{
}

/* We don't actually allow hardware surfaces other than the main one */
// TODO: use OpenGL textures here
static int ANDROID_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	return(-1);
}
static void ANDROID_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

/* We need to wait for vertical retrace on page flipped displays */
static int ANDROID_LockHWSurface(_THIS, SDL_Surface *surface)
{
	return(0);
}

static void ANDROID_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

static void ANDROID_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
  int i, cx, cy, ip;

    //__android_log_print(ANDROID_LOG_INFO, "libSDL", "SDL_UpdateRects: numrects %i", numrects);
        
  for (i=0;i<numrects;i++) {
    
    if( ! sdl_opengl )
	{
            ip=0;
            for (cy=rects[i].y;cy<rects[i].y+rects[i].h;cy++) {
                for (cx=rects[i].x;cx<rects[i].x+rects[i].w;cx++) {
                    part_screen[ip] = memBuffer[cx+cy*memX];
                    ip++;
                }
            }

		    glTexSubImage2D(GL_TEXTURE_2D, 0, rects[i].x, rects[i].y, rects[i].w, rects[i].h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, part_screen);
#if SDL_VIDEO_RENDER_RESIZE
		    glDrawTexiOES(0, 0, 1, SDL_ANDROID_sWindowWidth, SDL_ANDROID_sWindowHeight);  // Stretch to screen
#else
		    glDrawTexiOES(0, SDL_ANDROID_sWindowHeight - SDL_ANDROID_sFakeWindowHeight, 1, SDL_ANDROID_sFakeWindowWidth, SDL_ANDROID_sFakeWindowHeight);  // Do not stretch
#endif
    }

	SDL_ANDROID_CallJavaSwapBuffers();
  }

}

static int ANDROID_FlipHWSurface(_THIS, SDL_Surface *surface)
{
	if( ! sdl_opengl )
	{
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, memX, memY, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, memBuffer);
#if SDL_VIDEO_RENDER_RESIZE
		glDrawTexiOES(0, 0, 1, SDL_ANDROID_sWindowWidth, SDL_ANDROID_sWindowHeight);  // Stretch to screen
#else
		glDrawTexiOES(0, SDL_ANDROID_sWindowHeight - SDL_ANDROID_sFakeWindowHeight, 1, SDL_ANDROID_sFakeWindowWidth, SDL_ANDROID_sFakeWindowHeight);  // Do not stretch
#endif
	}

	SDL_ANDROID_CallJavaSwapBuffers();

	return(0);
};

void ANDROID_GL_SwapBuffers(_THIS)
{
	SDL_ANDROID_CallJavaSwapBuffers();
};

int ANDROID_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
	return(1);
}

void SdlGlRenderInit()
{
	int textX, textY;
	
	if( !sdl_opengl && memBuffer )
	{
			// Texture sizes should be 2^n SQUARE!
			textX = 1024;
			textY = 1024;

			if( memX <= 512 && memY <=512 ) {
				textX = 512;
				textY = 512;
            }

            if( memX <= 256 && memY <=256 ) {
				textX = 256;
				textY = 256;
            }

			void * textBuffer = SDL_malloc( textX*textY*2 );
			SDL_memset( textBuffer, 0, textX*textY*2 );

    /* GL Init */
    glBindTexture(GL_TEXTURE_2D, 0);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if SDL_VIDEO_RENDER_RESIZE    
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#else
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameterx(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
#endif
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DITHER);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_MULTISAMPLE);
    glDisable(GL_CULL_FACE);
    glShadeModel(GL_FLAT);
    GLint crop[4] = { 0, memY, memX, -memY };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textX, textY, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, textBuffer);
    //glDrawTexiOES(0, 0, 1, memX, memY);
    glFinish();
    /* finish GL init */

			
			SDL_free( textBuffer );
	}
}
