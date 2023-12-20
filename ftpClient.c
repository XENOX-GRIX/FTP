#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>  
#include <ctype.h>
#include <fcntl.h> 
#include <sys/wait.h>
#include <sys/stat.h>

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


int cd(int sock, char splitStrings[10][100]){
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, "cd ");
    strcat(buffer, splitStrings[1]);
    send(sock, buffer, strlen(buffer), 0);
    return 1; 
}


int ls(int sock){
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, "ls");
    printf("Client: %s\n", buffer);
    send(sock, buffer, strlen(buffer), 0);

    bzero(buffer, 1024);
    int n = recv(sock, buffer, sizeof(buffer), 0);
    printf("%s\n", buffer);
    if(n){
        return 1; 
    }
    return -1; 
}

int pwd(int sock){
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, "pwd");
    send(sock, buffer, strlen(buffer), 0);

    bzero(buffer, 1024);
    int n = recv(sock, buffer, sizeof(buffer), 0);
    for(int i = 0; i<sizeof(buffer); i++){
        if(buffer[i] == '\n'){
            buffer[i] = '\0'; 
        }
    }
    printf("%s", buffer);
    if(n){
        return 1; 
    }
    return -1; 
}
int path(int sock){
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, "path");
    send(sock, buffer, strlen(buffer), 0);

    bzero(buffer, 1024);
    int n = recv(sock, buffer, sizeof(buffer), 0);
    for(int i = 0; i<sizeof(buffer); i++){
        if(buffer[i] == '\n'){
            buffer[i] = '\0'; 
        }
    }
    printf("%s", buffer);
    if(n){
        return 1; 
    }
    return -1; 
}

void sendCloseSignal(int sock){
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, "close");
    send(sock, buffer, strlen(buffer), 0);
}


int put(int sock, char splitStrings[10][100]){
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, "put ");
    strcat(buffer, splitStrings[1]); 
    printf("Sending :%s\n", buffer); 
    send(sock, buffer, strlen(buffer), 0);

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


void get(int sock, char splitStrings[10][100]){
    const char *file_name = splitStrings[1];
    int file_fd = open(file_name, O_CREAT | O_WRONLY,  S_IRUSR | S_IWUSR);
    if (file_fd == -1) {
        perror("Failed to create the local file");
        exit(EXIT_FAILURE);
    }
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, "get ");
    strcat(buffer, splitStrings[1]); 
    send(sock, buffer, strlen(buffer), 0);

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


int main(int argc, char *argv[]){

    char *ip = "127.0.0.1";
    int port = 5566; 

    if (argc != 3) {
        printf("Usage: %s <server_ip> <server_port>\n\n", argv[0]);
        printf("Using the following : \n"); 
        printf("Server IP: %s\n", ip);
        printf("Server Port: %d\n\n", port);
    }
    else{
        ip = argv[1];
        port = atoi(argv[4]);  
    }

    int sock;
    struct timespec start_time, end_time;
    struct sockaddr_in addr;
    socklen_t addr_size;
    char buffer[1024];
    int n;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        perror("[-]Socket error");
        exit(1);
    }
    printf("[+]TCP server socket created.\n");


    memset(&addr, '\0', sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = port;
    addr.sin_addr.s_addr = inet_addr(ip);

    connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    printf("Connected to the server.\n"); 

    const int MAX_LIMIT = 100; 
    while (1){
        printf("FTP-Client(");
        path(sock);
        printf(")>> ");
        char cmd[MAX_LIMIT];
        fgets(cmd, MAX_LIMIT, stdin);

        // size_t len = strlen(cmd);
        // if (len > 0 && cmd[len - 1] == '\n') {
        //     cmd[len - 1] = '\0';
        // }

        char splitStrings[10][100];
        int amp = 0, i, cnt = 0;

        trim(cmd, splitStrings, &cnt);

        if (strcmp(splitStrings[0], "close") == 0) {
            sendCloseSignal(sock);
            break;
        }
        // if command is pwd
        else if (strcmp(splitStrings[0], "put") == 0) { 
            put(sock, splitStrings);
        }   

        // If commanbd is ls
        else if (strcmp(splitStrings[0], "get") == 0) {
            get(sock, splitStrings);
        }

        // If commanbd is mkdir
        else if (strcmp(splitStrings[0], "ls") == 0){
            ls(sock); 
        }

        // If commanbd is cd
        else if (strcmp(splitStrings[0], "cd") == 0) {
            if(cd(sock, splitStrings) == -1){
                printf("Failed to execute cd .. \n"); 
            }
        }
    }
    close(sock);
    return 0;
}
















