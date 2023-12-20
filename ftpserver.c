#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>  
#include <pthread.h>
#include <string.h> 
#include <ctype.h> 
#include <time.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/stat.h>

#define TIMEOUT_SECONDS 5

typedef struct threadData{
    int client_socket; 
    struct sockaddr_in client_addr; 
} threadData;

FILE *fp; 
time_t currentTime ; 
int fileSock; 

char *hope; 
void trim(char* str, char splitStrings[10][100], int *cnt) {
    int i, j;
    j = 0;
    *cnt = 0;
    int len = strlen(str);
    
    // Remove trailing whitespace
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\n')) {
        str[len - 1] = '\0';
        len--;
    }
    
    for (i = 0; i <= len && *cnt < 10; i++) {
        if (i == len) {
            splitStrings[*cnt][j] = '\0';
            (*cnt)++;
            return;
        }
        if (str[i] == ' ' || str[i] == '\0') {
            splitStrings[*cnt][j] = '\0';
            (*cnt)++;
            j = 0;
        } else {
            splitStrings[*cnt][j] = str[i];
            if(*cnt == 0){
                splitStrings[*cnt][j] = tolower(splitStrings[*cnt][j]);
            }
            j++;
        }
    }
    return;
}

void ls(int sock){
    FILE *fp = popen("ls", "r"); 
    size_t bytes_read; 
    char buffer[1024];
    bzero(buffer, 1024);
    bytes_read = fread(buffer , 1, sizeof(buffer), fp);
    send(sock, buffer,bytes_read, 0);
}

void pwd(int sock){
    FILE *fp = popen("pwd", "r"); 
    size_t bytes_read; 
    char buffer[1024];
    bzero(buffer, 1024);
    bytes_read = fread(buffer , 1, sizeof(buffer), fp);
    send(sock, buffer,bytes_read, 0);
}

void cd(int sock, char splitStrings[10][100]){
    if (strcmp(splitStrings[0], "cd") == 0) {
        if (chdir(splitStrings[1]) != 0){
            perror("chdir");
        }
    }
}

void receiveFile(int sock, char splitStrings[10][100]){
    const char *file_name = splitStrings[1];
    int file_fd = open(file_name, O_CREAT | O_WRONLY,  S_IRUSR | S_IWUSR);
    if (file_fd == -1) {
        perror("Failed to create the local file");
        exit(EXIT_FAILURE);
    }
    char buffer[1024];
    bzero(buffer, 1024);
    off_t remaining_bytes;
    off_t file_size;
    ssize_t bytes_received; 
    ssize_t n = recv(sock, &file_size, sizeof(off_t), 0);

    remaining_bytes = file_size;

    // Notify the client that the server is ready to receive the file
    char notify_ready[5] = "ready";
    send(sock, notify_ready, sizeof(notify_ready), 0);
    // Receive and write the file data
    while (remaining_bytes > 0) {
        bytes_received = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes_received < 0) {
            perror("Failed to receive file content");
            break;
        }
        if (bytes_received == 0) {
            // Connection closed by the client
            break;
        }
        write(file_fd, buffer, bytes_received);
        remaining_bytes -= bytes_received;
        send(sock, notify_ready, sizeof(notify_ready), 0);
    }
    // Close the local file and the client socket
    close(file_fd);
}

int sendFile(int sock, char splitStrings[10][100]) {
    char buffer[1024];
    bzero(buffer, 1024);

    int file_fd = open(splitStrings[1], O_RDONLY);
    if (file_fd == -1) {
        perror("Failed to open the file");
        close(sock);
        exit(EXIT_FAILURE);
    }

    struct stat file_stat;
    if (fstat(file_fd, &file_stat) == -1) {
        perror("Failed to get file size");
        close(file_fd);
        close(sock); 
        exit(EXIT_FAILURE);
    }
    off_t file_size = file_stat.st_size;

    // Send the file size and the file name as a header
    size_t name_length = strlen(splitStrings[1]);
    send(sock, &file_size, sizeof(off_t), 0);

    // Send the file's contents to the client
    bzero(buffer, 1024);
    ssize_t remaining_bytes = file_size;
    ssize_t bytes_read ; 

    bzero(buffer, 1024);

    // Wait for the server to notify readiness
    char notify_ready[5];
    ssize_t notify_ready_received = recv(sock, notify_ready, sizeof(notify_ready), 0);
    if (notify_ready_received <= 0) {
        perror("Failed to receive readiness notification");
        return 0;
    }

    if (strncmp(notify_ready, "ready", 5) != 0) {
        printf("Server is not ready for the file transfer.\n");
        return 0;
    }

    // Send the file data
    while (remaining_bytes > 0) {
        bytes_read = read(file_fd, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            perror("Failed to read file content");
            break;
        }
        if (bytes_read == 0) {
            // End of file
            break;
        }
        ssize_t bytes_sent = send(sock, buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Failed to send file content");
            break;
        }
        remaining_bytes -= bytes_sent;
        recv(sock, notify_ready, sizeof(notify_ready), 0);
    }

    close(file_fd);
    return 1; 
}

void logRequest(const char* id, const char* request, int r) {
    FILE *logFile = fopen("logfile.txt", "a");

    if (logFile == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    // Obtain the file descriptor from FILE*
    int logFileDescriptor = fileno(logFile);

    // Acquire an exclusive lock on the file
    if (flock(logFileDescriptor, LOCK_EX) == -1) {
        perror("Failed to lock log file");
        fclose(logFile);
        exit(EXIT_FAILURE);
    }

    currentTime = time(NULL); 
    if(r == 1) fprintf(logFile, "[+][id]Request[%ld]: ", currentTime);
    fprintf(logFile, "%s\n", request);

    // Release the lock and close the file
    if (flock(logFileDescriptor, LOCK_UN) == -1) {
        perror("Failed to unlock log file");
    }

    fclose(logFile);
}

void handleClient(void *args){
    pid_t child_pid;
    child_pid = fork(); 
    if (child_pid == -1){
        perror("Fork failed");
        exit(1);
    }
    if (child_pid == 0){
        // inside child
        threadData td = *(threadData*)args; 
        free(args); 
        int client_sock = td.client_socket; 
        struct sockaddr_in client_addr = td.client_addr;
        struct timespec start_time, end_time;
        char buffer[1024];
        char ack[1024]; 
        int n;
        long long rtt; 

        // Get the client's IP address and port
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        int client_port = ntohs(client_addr.sin_port);

        char logBuffer[1000];  // Adjust the size based on your needs
        char clientid[100]; 
        sprintf(logBuffer, "[+]Client %s:%d Connected", client_ip, client_port);
        sprintf(clientid, "%s:%d", client_ip, client_port);
        logRequest(clientid, logBuffer, 1);


        while (1) {
            bzero(buffer, 1024);
            n = recv(client_sock, buffer, sizeof(buffer), 0);
            char splitStrings[10][100];
            int amp = 0, i, cnt = 0;

            trim(buffer, splitStrings, &cnt);
            // printf("Client: %s\n", buffer);
            if (strcmp(splitStrings[0], "close") == 0) {
                break;
            }
            else if (strcmp(splitStrings[0], "path") == 0) { 
                pwd(client_sock);
            }
            else if (strcmp(splitStrings[0], "pwd") == 0) {
                sprintf(logBuffer, "pwd"); 
                logRequest(clientid,logBuffer, 1); 
                pwd(client_sock);
                logRequest(clientid, "pwd Successful", 1); 
            }
            // if command is put
            else if (strcmp(splitStrings[0], "put") == 0) {
                sprintf(logBuffer, "put %s", splitStrings[1]); 
                logRequest(clientid, logBuffer, 1);
                receiveFile(client_sock , splitStrings);
                logRequest(clientid, "put Successful", 1); 
            }

            // If commanbd is get
            else if (strcmp(splitStrings[0], "get") == 0) {
                sprintf(logBuffer, "get %s", splitStrings[1]); 
                logRequest(clientid, logBuffer, 1);
                sendFile(client_sock, splitStrings); 
                logRequest(clientid, "get Successful", 1); 
            }

            // If commanbd is ls
            else if (strcmp(splitStrings[0], "ls") == 0){
                sprintf(logBuffer, "ls"); 
                logRequest(clientid, logBuffer, 1);
                ls(client_sock); 
                logRequest(clientid, "ls Successful", 1); 
            }

            // If commanbd is cd
            else if (strcmp(splitStrings[0], "cd") == 0) {
                sprintf(logBuffer, "cd %s", splitStrings[1]); 
                logRequest(clientid, logBuffer, 1);
                cd(client_sock, splitStrings); 
                logRequest(clientid, "cd Successful", 1);
            }
        }
        close(client_sock);
        sprintf(logBuffer, "[+]Client %s:%d disconnected.\n", client_ip, client_port);
        logRequest(clientid,logBuffer, 1);
        exit(1); 
    }
}



int main(){
    char *ip = "127.0.0.1";
    int port = 5566;
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    struct timespec start_time, end_time;
    socklen_t addr_size;
    char buffer[1024];
    char ack[1024]; 
    int n;

    currentTime = time(NULL);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0){
        perror("[-]Socket error");
        exit(1);
    }
    printf("[+]TCP server socket created | timestamp : %ld .\n", currentTime);
    sprintf(buffer,"[+]TCP server socket created | timestamp : %ld .\n", currentTime);
    logRequest("", buffer, 0); 

    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = port;
    server_addr.sin_addr.s_addr = inet_addr(ip);

    n = bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (n < 0){
        perror("[-]Bind error");
        exit(1);
    }
    printf("[+]Bind to the port number: %d\n", port);
    sprintf(buffer,"[+]Bind to the port number: %d\n", port);
    logRequest("", buffer, 0);

    listen(server_sock, 5);
    // printf("Listening...\n");
    long long rtt; 
    // fp = fopen("server.txt","w"); 
    printf("Listening...\n");
    sprintf(buffer,"Listening...\n");
    logRequest("",buffer, 0);


    while(1){
        struct sockaddr_in client_addr;
        addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);
        int client_port = ntohs(client_addr.sin_port);

        int *pclient = malloc(sizeof(int)); 
        pthread_t t; 
        *pclient = client_sock; 
        threadData* data = malloc(sizeof(threadData));
        data->client_socket = client_sock;
        data->client_addr = client_addr;
        // int create_result = pthread_create(&t, NULL, handleClient, data);
        handleClient(data); 
    }
    printf("[+]Client dis-connected.\n");
    fclose(fp); 
    return 0;
}