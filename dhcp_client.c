#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <regex.h>
#include <time.h>

#define SERVER_IP "255.255.255.255"
#define SERVER_PORT 67
#define CLIENT_PORT 68

char *IP = NULL;
char *MASK = NULL;
char *DNS = NULL;
char *GATEWAY = NULL;
int LEASE_TIME = 0;
time_t lease_expiration_time = 0;
pthread_t lease_thread;
int is_released = 0;

void *timer(void *arg) {
    while (1) {
        if (lease_expiration_time > 0) {
            time_t current_time = time(NULL);
            int remaining_time = (int)difftime(lease_expiration_time, current_time);
            if (remaining_time <= 0 && !is_released) {
                printf("Ip lease has expired.\n");
                IP = MASK = DNS = GATEWAY = NULL;
                LEASE_TIME = 0;
                lease_expiration_time = 0;
                break;
            }
        }
        sleep(1);
    }
    return NULL;
}

void extract_values(char *response) {
    char temp[100]; // Buffer temporal para almacenar valores extraídos.

    // Extraer IP
    char *ip_start = strstr(response, "IP: ");
    if (ip_start != NULL) {
        sscanf(ip_start, "IP: %s", temp);
        IP = strdup(temp); // Almacena la IP en la variable global.
    } else {
        printf("Error extracting IP.\n");
    }

    // Extraer Mask
    char *mask_start = strstr(response, "Mask: ");
    if (mask_start != NULL) {
        sscanf(mask_start, "Mask: %s", temp);
        MASK = strdup(temp); // Almacena la máscara de red.
    } else {
        printf("Error extracting Mask.\n");
    }

    // Extraer DNS
    char *dns_start = strstr(response, "DNS: ");
    if (dns_start != NULL) {
        sscanf(dns_start, "DNS: %s", temp);
        DNS = strdup(temp); // Almacena la DNS.
    } else {
        printf("Error extracting DNS.\n");
    }

    // Extraer Gateway
    char *gateway_start = strstr(response, "Gateway: ");
    if (gateway_start != NULL) {
        sscanf(gateway_start, "Gateway: %s", temp);
        GATEWAY = strdup(temp); // Almacena la puerta de enlace.
    } else {
        printf("Error extracting Gateway.\n");
    }

    // Extraer Lease time
    char *lease_time_start = strstr(response, "Lease time: ");
    if (lease_time_start != NULL) {
        sscanf(lease_time_start, "Lease time: %d", &LEASE_TIME); // Extrae el tiempo de arrendamiento.
    } else {
        printf("Error extracting lease time.\n");
    }

    // Si el tiempo de arrendamiento es válido, inicia el hilo de temporizador.
    if (LEASE_TIME > 0) {
        lease_expiration_time = time(NULL) + LEASE_TIME;
        pthread_create(&lease_thread, NULL, timer, NULL);
    }
}


void dhcp_release() {
    int client_socket;
    struct sockaddr_in server_addr;
    char message[256];

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(GATEWAY);

    sprintf(message, "DHCPRELEASE: %s", IP);
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    close(client_socket);
}

void dhcp_request(char *ip, char *mask, char *dns, char *gateway, int lease_time) {
    int client_socket;
    struct sockaddr_in server_addr;
    char message[256];
    char response[1024];

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(gateway);

    sprintf(message, "DHCPREQUEST: %s", ip);
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    recvfrom(client_socket, response, 1024, 0, NULL, NULL);
    printf("Server response: %s\n", response);

    if (strstr(response, "OK")) {
        lease_expiration_time = time(NULL) + lease_time;
    } else {
        printf("Request error.\n");
        printf("IP: 169.254.0.1\nMask: 255.255.0.0\nDNS: 8.8.8.8\nGateway: ''\n");
        lease_expiration_time = 0;
    }

    close(client_socket);
}

void dhcp_renew() {
    int client_socket;
    struct sockaddr_in server_addr;
    char message[256];
    char response[1024];

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(GATEWAY);

    sprintf(message, "DHCPRENEW: %s", IP);
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    recvfrom(client_socket, response, 1024, 0, NULL, NULL);
    
    if (strstr(response, "renewed")) {
        lease_expiration_time = time(NULL) + LEASE_TIME;
        printf("IP renewed successfully. Lease time extended.\n");
    } else {
        printf("Renewal failed.\n");
    }

    close(client_socket);
}

void dhcp_discover() {
    int client_socket;
    struct sockaddr_in server_addr;
    char message[256];
    char response[1024];

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    
    int broadcastEnable = 1;
    setsockopt(client_socket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    strcpy(message, "DHCPDISCOVER");
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    recvfrom(client_socket, response, 1024, 0, NULL, NULL);
    
    if (strstr(response, "No IPs available.") == NULL) {
        extract_values(response);
        dhcp_request(IP, MASK, DNS, GATEWAY, LEASE_TIME);
    } else {
        printf("The server has no available IPs.\n");
    }

    close(client_socket);
}

int main() {
    int option;
    
    while (1) {
        printf("\n-------------------------------------------------------\n");
        printf("Press 1 for DHCP_DISCOVER Broadcast\n");
        printf("Press 2 to renew the current IP\n");
        scanf("%d", &option);

        if (option == 1) {
            if (IP == NULL) {
                dhcp_discover();
                is_released = 0;
            } else {
                printf("You already have an IP:\n");
            }
            printf(" Ip: %s \n Mask: %s \n Gateway: %s \n DNSGateway: %s \n", IP, MASK, GATEWAY, DNS);

        } else if (option == 2) {
            if (IP != NULL) {
                dhcp_renew();
                printf(" Ip: %s \n Mask: %s \n Gateway: %s \n DNSGateway: %s \n", IP, MASK, GATEWAY, DNS);
            } else {
                printf("No IP assigned yet. Use DHCP_DISCOVER first.\n");
            }
        } else {
            if (IP != NULL) {
                dhcp_release();
            }
            return 0;
        }
    }
}

