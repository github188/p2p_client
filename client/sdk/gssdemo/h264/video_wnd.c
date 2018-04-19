#include "video_wnd.h"
#include <Windows.h>
#include <tchar.h>

typedef struct video_wnd
{
	HWND wnd;
	HANDLE thread;
	HANDLE create_event;
}video_wnd;

#define VIDEO_WND_STRING _T("video_wnd")

HRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

DWORD WINAPI ThreadProc( LPVOID arg)
{
	WNDCLASSEX wcex;
	HWND hWnd;
	MSG msg;
	video_wnd* wnd = (video_wnd*)arg;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= NULL;
	wcex.hIcon			= NULL;
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= NULL;
	wcex.lpszClassName	= VIDEO_WND_STRING;
	wcex.hIconSm		= NULL;

	RegisterClassEx(&wcex);

	hWnd = CreateWindow(VIDEO_WND_STRING, VIDEO_WND_STRING, WS_OVERLAPPED|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, NULL, NULL);
	wnd->wnd = hWnd;
	SetEvent(wnd->create_event);
	if (!hWnd)
	{
		return -1;
	}

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);
	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

struct video_wnd* create_video_wnd()
{
	video_wnd* wnd;
	wnd = (video_wnd*)malloc(sizeof(video_wnd));
	
	wnd->wnd = NULL;
	wnd->create_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	wnd->thread = CreateThread(NULL, 0, ThreadProc, wnd, 0, NULL);

	//wait for video window create completed
	WaitForSingleObject(wnd->create_event, INFINITE);
	return wnd;
}

void destroy_video_wnd(struct video_wnd* wnd)
{
	if(wnd == NULL)
		return;
	if(IsWindow(wnd->wnd))
	{
		PostMessage(wnd->wnd, WM_DESTROY, 0, 0);
		DestroyWindow(wnd->wnd);
	}
	WaitForSingleObject(wnd->thread, INFINITE);

	CloseHandle(wnd->thread);
	CloseHandle(wnd->create_event);
	free(wnd);
}

HWND video_wnd_get_handle(struct video_wnd* wnd)
{
	if(wnd == NULL)
		return NULL;
	return wnd->wnd;
}