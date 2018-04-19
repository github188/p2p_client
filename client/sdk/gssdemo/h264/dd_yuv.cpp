#include "dd_yuv.h"
#include <ddraw.h>

#pragma comment(lib, "ddraw.lib") 
#pragma comment(lib, "dxguid.lib")

typedef struct direct_draw_yuv
{
	HWND wnd;

	LPDIRECTDRAW7           lpDD;    // DirectDraw object
	LPDIRECTDRAWSURFACE7    lpDDSPrimary;  // DirectDraw primary surface
	LPDIRECTDRAWSURFACE7    lpDDSOffScr;  // DirectDraw off screen surface
	DDSURFACEDESC2 ddsd; //DirectDraw surface desc
}direct_draw_yuv;

struct direct_draw_yuv* create_dd_yuv(HWND wnd, int video_width, int video_height)
{
	LPDIRECTDRAW7           lpDD = NULL;    // DirectDraw object
	LPDIRECTDRAWSURFACE7    lpDDSPrimary = NULL;  // DirectDraw primary surface
	LPDIRECTDRAWSURFACE7    lpDDSOffScr = NULL;  // DirectDraw yuv off screen surface
	DDSURFACEDESC2   ddsd;    // DirectDraw surface desc
	HRESULT hr;

	//create DirectDraw object
	hr = DirectDrawCreateEx(NULL, (VOID**)&lpDD, IID_IDirectDraw7, NULL);
	if (hr != DD_OK) 
		return NULL;

	//set cooperative level
	hr = lpDD->SetCooperativeLevel(wnd, DDSCL_NORMAL);
    if (hr != DD_OK)
	{
		lpDD->Release();
		return NULL;
	}

    //create primary surface
	ZeroMemory(&ddsd, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS;
	ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
	hr = lpDD->CreateSurface(&ddsd, &lpDDSPrimary, NULL);
	if (hr != DD_OK)
	{
		lpDD->Release();
		return NULL;
	}
    
	LPDIRECTDRAWCLIPPER  pcClipper; 
	hr = lpDD->CreateClipper( 0, &pcClipper, NULL );
	if(hr != DD_OK )
	{
		lpDDSPrimary->Release();
		lpDD->Release();
		return NULL;
	}
	hr = pcClipper->SetHWnd( 0, wnd );
	if(hr  != DD_OK )
	{
		lpDDSPrimary->Release();
		lpDD->Release();
		pcClipper->Release();
		return NULL;
	}
	hr = lpDDSPrimary->SetClipper( pcClipper );
	if(hr  != DD_OK )
	{
		lpDDSPrimary->Release();
		lpDD->Release();
		pcClipper->Release();
		return NULL;
	}
	pcClipper->Release();

	//create yuv off screen surface
	ZeroMemory(&ddsd, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	ddsd.dwWidth = video_width;
	ddsd.dwHeight = video_height;
	ddsd.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
	ddsd.ddpfPixelFormat.dwFlags  = DDPF_FOURCC | DDPF_YUV ;
	ddsd.ddpfPixelFormat.dwFourCC = MAKEFOURCC('Y','V', '1', '2');
	ddsd.ddpfPixelFormat.dwYUVBitCount = 8;
	hr = lpDD->CreateSurface(&ddsd, &lpDDSOffScr, NULL);
	if (hr != DD_OK)
	{
		lpDDSPrimary->Release();
		lpDD->Release();
		return NULL;
	}

	direct_draw_yuv* dd_yuv;
	dd_yuv = (direct_draw_yuv*)malloc(sizeof(direct_draw_yuv));
	dd_yuv->wnd = wnd;
	dd_yuv->lpDD = lpDD;
	dd_yuv->lpDDSPrimary = lpDDSPrimary;
	dd_yuv->lpDDSOffScr = lpDDSOffScr;
	memcpy(&dd_yuv->ddsd, &ddsd, sizeof(DDSURFACEDESC2));

	return dd_yuv;
}

void destroy_dd_yuv(struct direct_draw_yuv* dd_yuv)
{
	dd_yuv->lpDD->Release();
	dd_yuv->lpDDSPrimary->Release();
	dd_yuv->lpDDSOffScr->Release();
	free(dd_yuv);
}

void dd_yuv_draw(struct direct_draw_yuv* dd_yuv, unsigned char** data, int* linesize)
{
	LPBYTE lpSurf = (LPBYTE)dd_yuv->ddsd.lpSurface;
	LPBYTE PtrY = data[0];
	LPBYTE PtrU = data[1];
	LPBYTE PtrV = data[2];
	HRESULT result;

	do {
		result = dd_yuv->lpDDSOffScr->Lock(NULL, &dd_yuv->ddsd, DDLOCK_WAIT|DDLOCK_WRITEONLY, NULL);
	} while(result == DDERR_WASSTILLDRAWING);
	if(result != DD_OK)
		return ;

	// fill yuv off screen surface
	if(lpSurf)
	{
		for (DWORD i=0; i<dd_yuv->ddsd.dwHeight; i++)
		{
			memcpy(lpSurf, PtrY, dd_yuv->ddsd.dwWidth);
			PtrY += linesize[0];
			lpSurf += dd_yuv->ddsd.lPitch;
		}
		for (DWORD i=0; i<dd_yuv->ddsd.dwHeight/2; i++)
		{
			memcpy(lpSurf, PtrV, dd_yuv->ddsd.dwWidth/2);
			PtrV += linesize[1]; 
			lpSurf += dd_yuv->ddsd.lPitch/2;
		}
		for (DWORD i=0; i<dd_yuv->ddsd.dwHeight/2; i++)
		{
			memcpy(lpSurf, PtrU, dd_yuv->ddsd.dwWidth/2);
			PtrU += linesize[2];
			lpSurf += dd_yuv->ddsd.lPitch/2;
		}
	}

	dd_yuv->lpDDSOffScr->Unlock(NULL);
	
	if(lpSurf) //bit yuv surface to primary surface
	{
		RECT primary_rect;
		::GetClientRect(dd_yuv->wnd, &primary_rect);

		POINT pt;
		pt.x = primary_rect.left;
		pt.y = primary_rect.top;
		ClientToScreen(dd_yuv->wnd, &pt);
		primary_rect.left = pt.x;
		primary_rect.top = pt.y;
		pt.x = primary_rect.right;
		pt.y = primary_rect.bottom;
		ClientToScreen(dd_yuv->wnd, &pt);
		primary_rect.right = pt.x;
		primary_rect.bottom = pt.y;
	
		result = dd_yuv->lpDDSPrimary->Blt(&primary_rect, dd_yuv->lpDDSOffScr, NULL, DDBLT_WAIT, NULL);
	}
}