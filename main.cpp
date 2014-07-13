#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <windows.h>
#include <process.h>

const unsigned char u_room[0x100] = { 0 }; // room name.
const char *room = (const char*)&u_room[0];
const char *name = "keisan-kun";
const char *server = "irc.livedoor.ne.jp";

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "User32.lib")
void DisplayError(char *pszAPI);
std::string ReadAndHandleOutput(HANDLE hPipeRead);
void PrepAndLaunchRedirectedChild(HANDLE hChildStdOut,
                                    HANDLE hChildStdIn,
                                    HANDLE hChildStdErr);
DWORD WINAPI GetAndSendInputThread(LPVOID lpvThreadParam);

HANDLE hChildProcess = NULL;
HANDLE hStdIn = NULL; // Handle to parents std input.
BOOL bRunThread = TRUE;

void WriteAndHandleInput(HANDLE hPipeWrite, const std::string &str)
{
    DWORD nBytesWrite;

    //while(TRUE)
    //{
        if (!WriteFile(hPipeWrite,str.c_str(),str.size(),
                                        &nBytesWrite,NULL) || !nBytesWrite)
        {
        if (GetLastError() == ERROR_BROKEN_PIPE)
            return; //break; // pipe done - normal exit path.
        else
            DisplayError("WriteFile"); // Something bad happened.
        }
    //}
}

HANDLE hOutputReadTmp,hOutputRead,hOutputWrite;
HANDLE hInputWriteTmp,hInputRead,hInputWrite;
HANDLE hErrorWrite;
HANDLE hThread;
DWORD ThreadId;
SECURITY_ATTRIBUTES sa;

void setup_cui_capture ()
{
    // Set up the security attributes struct.
    sa.nLength= sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;


    // Create the child output pipe.
    if (!CreatePipe(&hOutputReadTmp,&hOutputWrite,&sa,0))
        DisplayError("CreatePipe");


    // Create a duplicate of the output write handle for the std error
    // write handle. This is necessary in case the child application
    // closes one of its std output handles.
    if (!DuplicateHandle(GetCurrentProcess(),hOutputWrite,
                        GetCurrentProcess(),&hErrorWrite,0,
                        TRUE,DUPLICATE_SAME_ACCESS))
        DisplayError("DuplicateHandle");


    // Create the child input pipe.
    if (!CreatePipe(&hInputRead,&hInputWriteTmp,&sa,0))
        DisplayError("CreatePipe");


    // Create new output read handle and the input write handles. Set
    // the Properties to FALSE. Otherwise, the child inherits the
    // properties and, as a result, non-closeable handles to the pipes
    // are created.
    if (!DuplicateHandle(GetCurrentProcess(),hOutputReadTmp,
                        GetCurrentProcess(),
                        &hOutputRead, // Address of new handle.
                        0,FALSE, // Make it uninheritable.
                        DUPLICATE_SAME_ACCESS))
        DisplayError("DupliateHandle");

    if (!DuplicateHandle(GetCurrentProcess(),hInputWriteTmp,
                        GetCurrentProcess(),
                        &hInputWrite, // Address of new handle.
                        0,TRUE,
                        DUPLICATE_SAME_ACCESS))
    DisplayError("DupliateHandle");


    // Close inheritable copies of the handles you do not want to be
    // inherited.
    if (!CloseHandle(hOutputReadTmp)) DisplayError("CloseHandle");
    if (!CloseHandle(hInputWriteTmp)) DisplayError("CloseHandle");


    // Get std input handle so you can close it and force the ReadFile to
    // fail when you want the input thread to exit.
    if ( (hStdIn = GetStdHandle(STD_INPUT_HANDLE)) ==
                                            INVALID_HANDLE_VALUE )
        DisplayError("GetStdHandle");

    PrepAndLaunchRedirectedChild(hOutputWrite,hInputRead,hErrorWrite);


    // Close pipe handles (do not continue to modify the parent).
    // You need to make sure that no handles to the write end of the
    // output pipe are maintained in this process or else the pipe will
    // not close when the child process exits and the ReadFile will hang.
    if (!CloseHandle(hOutputWrite)) DisplayError("CloseHandle");
    if (!CloseHandle(hInputRead )) DisplayError("CloseHandle");
    if (!CloseHandle(hErrorWrite)) DisplayError("CloseHandle");

    // Launch the thread that gets the input and sends it to the child.
    hThread = CreateThread(NULL,0,GetAndSendInputThread,
                            (LPVOID)hInputWrite,0,&ThreadId);
    if (hThread == NULL) DisplayError("CreateThread");

    ReadAndHandleOutput(hOutputRead);
    ReadAndHandleOutput(hOutputRead);
}

std::string r_cui(){
    return ReadAndHandleOutput(hOutputRead);
}

std::string rw_cui(const std::string &str){
    WriteAndHandleInput(hInputWrite, "InputForm[" + str + "]\r\n");
    WaitForInputIdle(hThread, INFINITE);
    return ReadAndHandleOutput(hOutputRead);
}

void end_of_cui_capture(){
    // Force the read on the input to return by closing the stdin handle.
    if (!CloseHandle(hStdIn)) DisplayError("CloseHandle");

    // Tell the thread to exit and wait for thread to die.
    bRunThread = FALSE;

    if (WaitForSingleObject(hThread,INFINITE) == WAIT_FAILED)
        DisplayError("WaitForSingleObject");

    if (!CloseHandle(hOutputRead)) DisplayError("CloseHandle");
    if (!CloseHandle(hInputWrite)) DisplayError("CloseHandle");
}

int explode(char ***arr_ptr, char *str, char delimiter)
{
    char *src = str, *end, *dst;
    char **arr;
    int size = 1, i;

    // Find number of strings
    while ((end = strchr(src, delimiter)) != NULL)
    {
        ++size;
        src = end + 1;
    }

    arr = (char**)malloc(size * sizeof(char *)+(strlen(str) + 1) * sizeof(char));

    src = str;
    dst = (char *)arr + size * sizeof(char *);
    for (i = 0; i < size; ++i)
    {
        if ((end = strchr(src, delimiter)) == NULL)
        end = src + strlen(src);
        arr[i] = dst;
        strncpy(dst, src, end - src);
        dst[end - src] = '\0';
        dst += end - src + 1;
        src = end + 1;
    }
    *arr_ptr = arr;

    return size;
}

int KillSocket(SOCKET connection, FD_SET fds)
{
    FD_CLR(connection, &fds);
    closesocket(connection);
    return 1;
}

void ParseResponse(SOCKET connection, char* buffer)
{
    char **lines;
    int linecount, i;

    linecount = explode(&lines, buffer, '\n');

    for (i = 0; i < linecount; i++)
    {

        if (strcmp(lines[i], "") && strcmp(lines[i], "\r"))
        {

            char **words;
            int wordcount = explode(&words, lines[i], ' ');
            // commands start here
            if (!strcmp(words[0], "PING"))
            {
                char msg[512];

                sprintf(msg, "%s %s\r\n", "PONG", words[1]);
                msg[strlen(msg)] = '\0';
                send(connection, msg, strlen(msg), 0);
            }
            if (wordcount > 1)
            {

                if (!strcmp(words[1], "001"))
                {
                    char msg[512];
                    sprintf(msg, "JOIN #%s\r\n", room);
                    msg[strlen(msg)] = '\0';
                    send(connection, msg, strlen(msg), 0);
                }
                if (!strcmp(words[1], "PRIVMSG"))
                {
                    if(!strcmp(words[3], ":math")){
                        std::string line_str;
                        for(int i = 4; i < wordcount; ++i)
                        {
                            line_str += words[i];
                        }
                        line_str.pop_back();
                        std::string result_str;
                        static int n = 0;
                        if(n == 0){
                            ++n;
                            result_str = r_cui();
                        }
                        result_str = rw_cui(line_str);
                        for(; ; ){
                            result_str += r_cui();
                            std::size_t pos = result_str.rfind("]:=");
                            if(pos == std::string::npos)
                            {
                                continue;
                            }
                            break;
                        }
                        std::string put = std::string(result_str.begin() + result_str.find("//InputForm= ") + 13, result_str.begin() + result_str.rfind("]:="));
                        put.resize(put.rfind("In["));
                        put.erase(std::remove_if(put.begin(), put.end(), [](char c){ return c == '\r' || c == '\n'; }), put.end());
                        std::size_t pos = 0;
                        while((pos = put.find("  ")) != std::string::npos){
                            put.replace(pos, 2, " ");
                        }
                        char msg[0x1000];
                        sprintf(msg, "NOTICE #%s :%s\r\n", room, put.c_str());
                        msg[strlen(msg)] = '\0';
                        send(connection, msg, strlen(msg), 0);
                    }
                }
            }
            // End of commands
            free(words);
        }
    }

    free(lines);
}

SOCKET ConnectToServer(const char *ircserver, int port)
{
    struct hostent *h;
    SOCKADDR_IN c_sock_addr;

    SOCKET connection;
    connection = INVALID_SOCKET;
    connection = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (connection == INVALID_SOCKET)
    {
        //printf("Failed to create socket: %ld\r\n", connection);
    }
    else
    {
        //printf("Created socket: %ld\r\n", connection);
    }
    if ((h = gethostbyname(ircserver)) == NULL)
    {
        //printf("SID: %ld failed to resolve host: %s\r\n", connection, ircserver);
        return INVALID_SOCKET;
    }
    else
    {
        //printf ("SID: %ld resolved hostname: %s\r\n", connection, ircserver);
    }
    memset(&c_sock_addr, 0, sizeof(c_sock_addr));
    c_sock_addr.sin_family = AF_INET;
    c_sock_addr.sin_port = htons(port);
    c_sock_addr.sin_addr.s_addr = *((unsigned long*)h->h_addr);
    if (connect(connection, (SOCKADDR *)&c_sock_addr, sizeof(SOCKADDR_IN)) != 0)
    {
        //printf("Connection failed.\r\n");
        closesocket(connection);
        return INVALID_SOCKET;
    }
    unsigned long on = 1;
    if (0 != ioctlsocket(connection, FIONBIO, &on))
    {
        //printf("Failed\r\n");
    }
    char string[1024];
    // char nick[512];

    sprintf(string, "USER %s * * :%s\r\nNICK %s\r\n", name, name, name);
    //send(i, string, strlen(string)+1, 0);
    send(connection, string, strlen(string), 0);
    return connection;
}

DWORD Main_loop(LPVOID lpdwThreadParam)
{
    srand((unsigned int)time(NULL));
    FD_SET fds, fd_set_read;
    FD_ZERO(&fds);
    struct timeval tv = { 0 };
    SOCKET connections[2] = { 0 };
    int rcvdb;
    connections[0] = ConnectToServer(server, 6668);

    if (connections[0] != INVALID_SOCKET) FD_SET(connections[0], &fds);


    while (1)
    {
        fd_set_read = fds;
        if (select(FD_SETSIZE, &fd_set_read, NULL, NULL, NULL) < 0)
        {
            perror("select");
            return -1;
        }
        int i;

        for (i = 0; i < FD_SETSIZE * 10; i++)
        {
            if (FD_ISSET(i, &fd_set_read))
            {
                char buffer[4096];

                memset(&buffer, 0, sizeof(buffer));

                while ((rcvdb = recv(i, buffer, 4096, 0)) > 0)
                {
                    //printf("read more thann rf\r\n");
                    if (rcvdb <= 0)
                    {
                        KillSocket(i, fd_set_read);
                        return 0;

                    }


                    ParseResponse(i, buffer);
                    //int a;
                    //for (a = 0; a < (int)strlen(buffer); a++) putchar(buffer[a]);

                    memset(&buffer, 0, sizeof(buffer));

                }
            }
        }
    }

    return 0;
}

int main()
{
    setup_cui_capture();

    WSADATA wsaData;
    if (!WSAStartup(MAKEWORD(2, 0), &wsaData) == 0)
    {
        //printf("Could not WSAStartup\r\n");
        return 0;
    }
  
    DWORD threadID;
    HANDLE handleformainloop = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&Main_loop, 0, 0, &threadID);

    //printf("[+} Entered Main loop - %ld -\r\n", threadID);
    WaitForSingleObject(handleformainloop, INFINITE);
    CloseHandle(handleformainloop);
    //printf("[+] Main Loop returned\r\n");
    end_of_cui_capture();
    getchar();
    return 0;
}

int _main(){
    char buff[0x1000];
    setup_cui_capture();
    for(int i = 0; i < 40; ++i){
        sprintf_s(buff, "Integrate[(a/%d)^(x/(%d)), x]", i + 1, (i + 1) * 2);
        rw_cui(buff);
        Sleep(100);
    }
    end_of_cui_capture();
    return 0;
}

/////////////////////////////////////////////////////////////////////// 
// PrepAndLaunchRedirectedChild
// Sets up STARTUPINFO structure, and launches redirected child.
/////////////////////////////////////////////////////////////////////// 
void PrepAndLaunchRedirectedChild(HANDLE hChildStdOut,
                                    HANDLE hChildStdIn,
                                    HANDLE hChildStdErr)
{
    PROCESS_INFORMATION pi;
    STARTUPINFO si;

    // Set up the start up info struct.
    ZeroMemory(&si,sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hChildStdOut;
    si.hStdInput  = hChildStdIn;
    si.hStdError  = hChildStdErr;
    // Use this if you want to hide the child:
    //     si.wShowWindow = SW_HIDE;
    // Note that dwFlags must include STARTF_USESHOWWINDOW if you want to
    // use the wShowWindow flags.


    // Launch the process that you want to redirect (in this case,
    // Child.exe). Make sure Child.exe is in the same directory as
    // redirect.c launch redirect from a command line to prevent location
    // confusion.
    if (!CreateProcess(NULL,"C:/Program Files/Wolfram Research/Mathematica/9.0/math",NULL,NULL,TRUE,
                        CREATE_NEW_CONSOLE,NULL,NULL,&si,&pi))
        DisplayError("CreateProcess");


    // Set global child process handle to cause threads to exit.
    hChildProcess = pi.hProcess;


    // Close any unnecessary handles.
    if (!CloseHandle(pi.hThread)) DisplayError("CloseHandle");
}


/////////////////////////////////////////////////////////////////////// 
// ReadAndHandleOutput
// Monitors handle for input. Exits when child exits or pipe breaks.
/////////////////////////////////////////////////////////////////////// 
std::string ReadAndHandleOutput(HANDLE hPipeRead)
{
    CHAR lpBuffer[0x1000];
    DWORD nBytesRead;
    DWORD nCharsWritten;

    //while(TRUE)
    //{
        if (!ReadFile(hPipeRead,lpBuffer,sizeof(lpBuffer) - 1,
                                        &nBytesRead,NULL) || !nBytesRead || nBytesRead >= sizeof(lpBuffer) - 1)
        {
            DisplayError("ReadFile"); // Something bad happened.
        }

        // Display the character read on the screen.
        if (!WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE),lpBuffer,
                        nBytesRead,&nCharsWritten,NULL))
        DisplayError("WriteConsole");
    //}

    lpBuffer[nBytesRead] = 0;
    return lpBuffer;
}


/////////////////////////////////////////////////////////////////////// 
// GetAndSendInputThread
// Thread procedure that monitors the console for input and sends input
// to the child process through the input pipe.
// This thread ends when the child application exits.
/////////////////////////////////////////////////////////////////////// 
DWORD WINAPI GetAndSendInputThread(LPVOID lpvThreadParam)
{
    //CHAR read_buff[256];
    //DWORD nBytesRead = 0,nBytesWrote;
    //HANDLE hPipeWrite = (HANDLE)lpvThreadParam;

    // Get input from our console and send it to child through the pipe.
    //while (bRunThread)
    //{
    //    //if(!ReadConsole(hStdIn,read_buff,1,&nBytesRead,NULL))
    //    //DisplayError("ReadConsole");

    //    read_buff[nBytesRead] = '\0'; // Follow input with a NULL.

    //    if (!WriteFile(hPipeWrite,read_buff,nBytesRead,&nBytesWrote,NULL))
    //    {
    //    if (GetLastError() == ERROR_NO_DATA)
    //        break; // Pipe was closed (normal exit path).
    //    else
    //    DisplayError("WriteFile");
    //    }
    //}

    return 0;
}

/////////////////////////////////////////////////////////////////////// 
// DisplayError
// Displays the error number and corresponding message.
/////////////////////////////////////////////////////////////////////// 
void DisplayError(char *pszAPI)
{
    LPVOID lpvMessageBuffer;
    CHAR szPrintBuffer[512];
    DWORD nCharsWritten;

    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
            NULL, GetLastError(),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&lpvMessageBuffer, 0, NULL);

    wsprintf(szPrintBuffer,
        "ERROR: API    = %s.\n   error code = %d.\n   message    = %s.\n",
            pszAPI, GetLastError(), (char *)lpvMessageBuffer);

    WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE),szPrintBuffer,
                    lstrlen(szPrintBuffer),&nCharsWritten,NULL);

    LocalFree(lpvMessageBuffer);
    ExitProcess(GetLastError());
}
