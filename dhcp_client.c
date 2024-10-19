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

void dhcp_release(char *ip);

void release_on_exit() {
    if (IP != NULL && !is_released) {
        printf("\nLiberando la IP automáticamente al finalizar la ejecución...\n");
        dhcp_release(IP);
    }
}

void dhcp_renew();
void dhcp_request(char *ip, char *mask, char *dns, char *gateway, int lease_time);

void print_dhcp_message(const char *message_type, const char *ip_offered, int lease_time) {
    printf("\n==================== DHCP MESSAGE ====================\n");
    printf("Operation Code (op)     : 1\n");
    printf("Transaction ID (xid)    : 0xA5462E39\n");  // Hardcoded for simplicity
    printf("Client IP Address       : 0.0.0.0\n");
    printf("Offered IP (Your IP)    : %s\n", ip_offered);
    printf("Server IP Address       : 127.0.0.1\n");  // Example
    printf("Gateway IP Address      : 127.0.0.2\n");  // Example
    printf("Client MAC Address      : 00:15:5d:9e:14:94\n");
    printf("Subnet Mask             : 255.255.255.0\n");
    printf("DNS Server              : 8.8.8.8\n");
    printf("\n==================== DHCP TYPE ====================\n");
    printf("DHCP Message Type    : %s\n", message_type);
    printf("IP Address Lease Time: %d seconds\n", lease_time);
    printf("======================================================\n");
}

void *timer(void *arg) {
    while (1) {
        if (lease_expiration_time > 0) {
            time_t current_time = time(NULL);
            int remaining_time = (int)difftime(lease_expiration_time, current_time);

            if (remaining_time == 50 && !is_released) {  // Notificar al usuario cuando queden 60 segundos
                printf("\nEl tiempo de arrendamiento está por expirar. ¿Deseas renovarlo? (s/n): ");
                char user_input;
                scanf(" %c", &user_input);

                if (user_input == 's' || user_input == 'S') {
                    dhcp_renew();
                } else {
                    printf("No se ha renovado el arrendamiento. La IP será liberada al expirar el tiempo.\n");
                }
            }

            if (remaining_time <= 0 && !is_released) {
                printf("El tiempo de arrendamiento ha expirado. Liberando IP...\n");
                dhcp_release(IP);
                IP = MASK = DNS = GATEWAY = NULL;
                LEASE_TIME = 0;
                lease_expiration_time = 0;
                is_released = 1;
                break;
            }
        }
        sleep(1);
    }
    return NULL;
}

void extract_values(char *response) {
    char temp[100];  // Buffer temporal para almacenar valores extraídos.

    // Extraer IP
    char *ip_start = strstr(response, "IP: ");
    if (ip_start != NULL) {
        sscanf(ip_start, "IP: %s", temp);
        IP = strdup(temp);  // Almacena la IP en la variable global.
    } else {
        printf("Error extracting IP.\n");
    }

    // Extraer Mask
    char *mask_start = strstr(response, "Mask: ");
    if (mask_start != NULL) {
        sscanf(mask_start, "Mask: %s", temp);
        MASK = strdup(temp);  // Almacena la máscara de red.
    } else {
        printf("Error extracting Mask.\n");
    }

    // Extraer DNS
    char *dns_start = strstr(response, "DNS: ");
    if (dns_start != NULL) {
        sscanf(dns_start, "DNS: %s", temp);
        DNS = strdup(temp);  // Almacena la DNS.
    } else {
        printf("Error extracting DNS.\n");
    }

    // Extraer Gateway
    char *gateway_start = strstr(response, "Gateway: ");
    if (gateway_start != NULL) {
        sscanf(gateway_start, "Gateway: %s", temp);
        GATEWAY = strdup(temp);  // Almacena la puerta de enlace.
    } else {
        printf("Error extracting Gateway.\n");
    }

    // Extraer Lease time
    char *lease_time_start = strstr(response, "Lease time: ");
    if (lease_time_start != NULL) {
        sscanf(lease_time_start, "Lease time: %d", &LEASE_TIME);  // Extrae el tiempo de arrendamiento.
    } else {
        printf("Error extracting lease time.\n");
    }

    // Si el tiempo de arrendamiento es válido, inicia el hilo de temporizador.
    if (LEASE_TIME > 0) {
        lease_expiration_time = time(NULL) + LEASE_TIME;
        pthread_create(&lease_thread, NULL, timer, NULL);
    }
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

    printf("DHCP Discover message sent to server.\n");
    strcpy(message, "DHCPDISCOVER");
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    recvfrom(client_socket, response, 1024, 0, NULL, NULL);
    
    if (strstr(response, "No IPs available.") == NULL) {
        extract_values(response);
        print_dhcp_message("2 (DHCP OFFER)", IP, LEASE_TIME);
        dhcp_request(IP, MASK, DNS, GATEWAY, LEASE_TIME);
    } else {
        printf("The server has no available IPs.\n");
    }

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
    printf("DHCP_REQUEST message sent to the server.\n");
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    recvfrom(client_socket, response, 1024, 0, NULL, NULL);
    printf("Server response: %s\n", response);

    if (strstr(response, "Acknowledged")) {
        lease_expiration_time = time(NULL) + lease_time;
        print_dhcp_message("5 (DHCP ACK)", ip, lease_time);
    } else {
        printf("Request error.\n");
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
    printf("Enviando solicitud DHCPRENEW para IP: %s\n", IP);
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    recvfrom(client_socket, response, 1024, 0, NULL, NULL);
    printf("Respuesta del servidor: %s\n", response);

    if (strstr(response, "renewed successfully")) {
        lease_expiration_time = time(NULL) + LEASE_TIME;
        print_dhcp_message("5 (DHCP RENEW)", IP, LEASE_TIME);
    } else {
        printf("Error en la renovación de la IP.\n");
    }

    close(client_socket);
}

void dhcp_release(char *ip) {
    int client_socket;
    struct sockaddr_in server_addr;
    char message[256];
    char response[1024];

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(GATEWAY);

    sprintf(message, "DHCPRELEASE: %s", ip);
    printf("Enviando solicitud DHCPRELEASE para liberar IP: %s\n", ip);
    sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

    recvfrom(client_socket, response, 1024, 0, NULL, NULL);
    printf("Respuesta del servidor: %s\n", response);

    if (strstr(response, "released successfully")) {
        printf("IP liberada exitosamente.\n");
        is_released = 1;  // Marcar como liberada
    } else {
        printf("Error al liberar la IP.\n");
    }

    close(client_socket);
}

int main() {
    // Registrar la función para liberar la IP al salir
    atexit(release_on_exit);

    printf("Starting DHCP client...\n");
    dhcp_discover();  // Automatización: iniciar con DHCP_DISCOVER
    printf("Assigned IP: %s\n", IP);  // Mostrar IP asignada

    while (1) {
        sleep(60);  // Mantener la ejecución en espera (o manejar renovaciones según sea necesario)
    }

    return 0;
}

