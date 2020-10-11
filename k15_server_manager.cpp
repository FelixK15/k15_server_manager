#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

#include "k15_std/include/k15_base.hpp"
#include "k15_html_server.hpp"

#include "k15_std/src/k15_memory.cpp"
//#include "k15_std/src/k15_format.cpp"
#include "k15_std/src/k15_profiling.cpp"
#include "k15_std/src/k15_time.cpp"
#include "k15_std/src/k15_string.cpp"
#include "k15_std/src/k15_platform_win32.cpp"
#include "k15_std/src/k15_profiling_win32.cpp"

#pragma comment( lib, "kernel32.lib" )
#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "ws2_32.lib" )

#define K15_FALSE 0
#define K15_TRUE  1

using namespace k15;

typedef LRESULT( CALLBACK* WNDPROC )( HWND, UINT, WPARAM, LPARAM );

void printErrorToFile( const char* p_FileName )
{
    DWORD errorId      = GetLastError();
    char* textBuffer   = 0;
    DWORD writtenChars = FormatMessage( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, 0, errorId,
                                        MAKELANGID( LANG_ENGLISH, SUBLANG_ENGLISH_US ), ( LPSTR )&textBuffer, 512, 0 );

    if ( writtenChars > 0 )
    {
        FILE* file = fopen( p_FileName, "w" );

        if ( file )
        {
            fwrite( textBuffer, writtenChars, 1, file );
            fflush( file );
            fclose( file );
        }
    }
}

void allocateDebugConsole()
{
    AllocConsole();
    AttachConsole( ATTACH_PARENT_PROCESS );
    freopen( "CONOUT$", "w", stdout );
}

void K15_WindowCreated( HWND p_HWND, UINT p_Message, WPARAM p_wParam, LPARAM p_lParam )
{
}

void K15_WindowClosed( HWND p_HWND, UINT p_Message, WPARAM p_wParam, LPARAM p_lParam )
{
}

void K15_KeyInput( HWND p_HWND, UINT p_Message, WPARAM p_wParam, LPARAM p_lParam )
{
}

void K15_MouseButtonInput( HWND p_HWND, UINT p_Message, WPARAM p_wParam, LPARAM p_lParam )
{
}

void K15_MouseMove( HWND p_HWND, UINT p_Message, WPARAM p_wParam, LPARAM p_lParam )
{
}

void K15_MouseWheel( HWND p_HWND, UINT p_Message, WPARAM p_wParam, LPARAM p_lParam )
{
}

LRESULT CALLBACK K15_WNDPROC( HWND p_HWND, UINT p_Message, WPARAM p_wParam, LPARAM p_lParam )
{
    bool8 messageHandled = K15_FALSE;

    switch ( p_Message )
    {
    case WM_CREATE:
        K15_WindowCreated( p_HWND, p_Message, p_wParam, p_lParam );
        break;

    case WM_CLOSE:
        K15_WindowClosed( p_HWND, p_Message, p_wParam, p_lParam );
        PostQuitMessage( 0 );
        messageHandled = K15_TRUE;
        break;

    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        K15_KeyInput( p_HWND, p_Message, p_wParam, p_lParam );
        break;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_XBUTTONUP:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        K15_MouseButtonInput( p_HWND, p_Message, p_wParam, p_lParam );
        break;

    case WM_MOUSEMOVE:
        K15_MouseMove( p_HWND, p_Message, p_wParam, p_lParam );
        break;

    case WM_MOUSEWHEEL:
        K15_MouseWheel( p_HWND, p_Message, p_wParam, p_lParam );
        break;
    }

    if ( messageHandled == K15_FALSE )
    {
        return DefWindowProc( p_HWND, p_Message, p_wParam, p_lParam );
    }

    return 0;
}

HWND setupWindow( HINSTANCE p_Instance, int p_Width, int p_Height )
{
    WNDCLASS wndClass      = { 0 };
    wndClass.style         = CS_HREDRAW | CS_OWNDC | CS_VREDRAW;
    wndClass.hInstance     = p_Instance;
    wndClass.lpszClassName = "K15_Win32Template";
    wndClass.lpfnWndProc   = K15_WNDPROC;
    wndClass.hCursor       = LoadCursor( NULL, IDC_ARROW );
    RegisterClass( &wndClass );

    HWND hwnd = CreateWindowA( "K15_Win32Template", "Win32 Template",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               p_Width, p_Height, 0, 0, p_Instance, 0 );

    if ( hwnd == INVALID_HANDLE_VALUE )
        MessageBox( 0, "Error creating Window.\n", "Error!", 0 );
    else
        ShowWindow( hwnd, SW_SHOW );
    return hwnd;
}

uint32 getTimeInMilliseconds( LARGE_INTEGER p_PerformanceFrequency )
{
    LARGE_INTEGER appTime = { 0 };
    QueryPerformanceFrequency( &appTime );

    appTime.QuadPart *= 1000; //to milliseconds

    return ( uint32 )( appTime.QuadPart / p_PerformanceFrequency.QuadPart );
}

void setup()
{
    allocateDebugConsole();
}

void doFrame( uint32 p_DeltaTimeInMS )
{
}

int CALLBACK WinMain( HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPSTR lpCmdLine, int nShowCmd )
{
    LARGE_INTEGER performanceFrequency;
    QueryPerformanceFrequency( &performanceFrequency );

    //FK: TODO: WSAGetLastError error handling
    setup();

    WSADATA wsa;
    if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
    {
        printf( "WSAStartup failed...\n" );
        return -1;
    }

    html_server_parameters parameters;
    parameters.port            = 9090;
    parameters.ipv4BindAddress = "0.0.0.0";
    parameters.ipv6BindAddress = "::0";
    parameters.pAllocator      = getCrtMemoryAllocator();

    result< html_server* > initResult = createHtmlServer( parameters );
    if ( initResult.hasError() )
    {
        printf( "Couldn't initialize html server.\n" );
        return -1;
    }

    html_server* pServer = initResult.getValue();
    return serveHtmlClients( pServer );

    return 0;
}