#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdint.h>

#define MAX_CHARACTERS 360
#define BUFFER_SIZE 1024
#define SOCKET_ADDRESS struct sockaddr
#define MAX_CLIENTS 100
#define DHCP_OPTIONS_LENGTH 312
#define HARDWARE_ADDR_LEN 16
#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_DECLINE 4
#define DHCP_ACK 5
#define DHCP_NAK 6
#define DHCP_RELEASE 7
#define DHCP_MAGIC_COOKIE 0x63825363
#define IP_ADDRESS_SIZE 16
#define LEASE_TIME 60
#define RESET "\033[0m"
#define BOLD "\033[1m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define MAGENTA "\033[35m"
#define RED "\033[31m"

// Definición de variables globales
int port = 1000;
char server_ip[] = "192.168.56.2";
char ip_range[] = "192.168.56.10-192.168.56.50";
char global_dns_ip[] = "8.8.8.8";
char global_subnet_mask[] = "255.255.255.0";

typedef struct {
    uint8_t op;
    uint8_t htype;
    uint8_t hlen;
    uint8_t hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t chaddr[HARDWARE_ADDR_LEN];
    uint8_t sname[64];
    uint8_t file[128];
    uint8_t options[DHCP_OPTIONS_LENGTH];
} dhcp_message_t;

typedef struct {
    int sockfd;
    struct sockaddr_in client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t client_addr_len;
} client_data_t;

typedef struct {
    char ip_address[IP_ADDRESS_SIZE];
    int is_assigned;
    time_t lease_start;
    int lease_duration;
} ip_pool_entry_t;

ip_pool_entry_t* ip_pool = NULL;
int pool_size = 0;
int sockfd;
char gateway_ip[16];

// Declaraciones de funciones
void init_dhcp_message(dhcp_message_t* msg);
int parse_dhcp_message(const uint8_t* buffer, dhcp_message_t* msg);
void set_dhcp_message_type(dhcp_message_t* msg, uint8_t type);
void print_dhcp_message(const dhcp_message_t* msg, int is_client);
int calculate_pool_size(char* start_ip, char* end_ip);
void generate_dynamic_gateway_ip(char* gateway_ip, size_t size);
int ip_to_int(const char* ip);
void int_to_ip(unsigned int ip, char* buffer);
char* assign_ip();
void release_ip(const char* ip);
void renew_lease(char* ip_address);
void check_leases();
void end_program();
void handle_signal_interrupt(int signal);
void send_dhcp_offer(int socket_fd, struct sockaddr_in* client_addr, dhcp_message_t* discover_message);
void handle_dhcp_request(int sockfd, struct sockaddr_in* client_addr, dhcp_message_t* request_msg);
void handle_dhcp_release(int sockfd, dhcp_message_t* release_msg);
void* process_client_connection(void* arg);
void* check_and_release(void* arg);

// Implementación de las funciones

void init_dhcp_message(dhcp_message_t* msg) {
    memset(msg, 0, sizeof(dhcp_message_t));
    msg->op = 2;  // BOOTREPLY
    msg->htype = 1;  // Ethernet
    msg->hlen = 6;  // Longitud de la MAC address
    msg->hops = 0;
    msg->xid = rand();  // Transaction ID
    msg->secs = 0;
    msg->flags = htons(0x8000);  // Bandera de Broadcast
}

int parse_dhcp_message(const uint8_t* buffer, dhcp_message_t* msg) {
    if (!buffer || !msg)
        return -1;
    memcpy(msg, buffer, sizeof(dhcp_message_t));
    msg->xid = ntohl(msg->xid);
    msg->secs = ntohs(msg->secs);
    msg->flags = ntohs(msg->flags);
    msg->ciaddr = ntohl(msg->ciaddr);
    msg->yiaddr = ntohl(msg->yiaddr);
    msg->siaddr = ntohl(msg->siaddr);
    msg->giaddr = ntohl(msg->giaddr);
    return 0;
}

void set_dhcp_message_type(dhcp_message_t* msg, uint8_t type) {
    msg->options[0] = 53;  // Tipo de mensaje DHCP
    msg->options[1] = 1;  // Longitud del campo de tipo de mensaje
    msg->options[2] = type;
}

void print_dhcp_message(const dhcp_message_t* msg, int is_client) {

    struct in_addr client_ip;
    client_ip.s_addr = is_client ? htonl(msg->ciaddr) : msg->ciaddr;

    struct in_addr your_ip;
    your_ip.s_addr = is_client ? htonl(msg->yiaddr) : msg->yiaddr;

    struct in_addr server_ip;
    server_ip.s_addr = is_client ? htonl(msg->siaddr) : msg->siaddr;

    struct in_addr gateway_ip;
    gateway_ip.s_addr = is_client ? htonl(msg->giaddr) : msg->giaddr;

    printf(BOLD YELLOW "\n==================== DHCP TYPE ====================\n" RESET);
}

// Genera la IP del gateway dinámicamente
void generate_dynamic_gateway_ip(char* gateway_ip, size_t size) {
    strncpy(gateway_ip, "127.0.0.1", size);  // IP del gateway por defecto
}

int calculate_pool_size(char* start_ip, char* end_ip) {
    unsigned int start = ip_to_int(start_ip);
    unsigned int end = ip_to_int(end_ip);
    return (end - start + 1);
}

int ip_to_int(const char* ip) {
    unsigned int a, b, c, d;
    sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d);
    return (a << 24) | (b << 16) | (c << 8) | d;
}

void int_to_ip(unsigned int ip, char* buffer) {
    sprintf(buffer, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

char* assign_ip() {
    for (int i = 0; i < pool_size; i++) {
        if (ip_pool[i].is_assigned == 0) {
            ip_pool[i].is_assigned = 1;
            time_t current_time = time(NULL);
            ip_pool[i].lease_start = current_time;
            ip_pool[i].lease_duration = LEASE_TIME;
            return ip_pool[i].ip_address;
        }
    }
    return NULL;
}

void release_ip(const char* ip) {
    for (int i = 0; i < pool_size; i++) {
        if (strcmp(ip_pool[i].ip_address, ip) == 0) {
            ip_pool[i].is_assigned = 0;
            return;
        }
    }
    printf("IP not found in pool: %s\n", ip);
}

void renew_lease(char* ip_address) {
    for (int i = 0; i < pool_size; i++) {
        if (strcmp(ip_pool[i].ip_address, ip_address) == 0) {
            ip_pool[i].lease_start = time(NULL);
            printf(GREEN "Lease renewed for IP address %s\n" RESET, ip_address);
            break;
        }
    }
}

// Funciones para el manejo del protocolo DHCP

// Manejo de señal SIGINT para terminar el programa
void handle_signal_interrupt(int signal) {
    printf("\nSignal %d received. Terminating...\n", signal);
    end_program();  // Llama a la función para cerrar recursos y salir del programa
}

// Función para liberar recursos y terminar el programa
void end_program() {
    if (sockfd >= 0) {
        close(sockfd);
    }
    if (ip_pool) {
        free(ip_pool);
    }
    printf("Exiting program...\n");
    exit(0);
}

// Función para manejar la conexión del cliente (procesar el mensaje DHCP)
void* process_client_connection(void* arg) {
    client_data_t* data = (client_data_t*)arg;
    char* buffer = data->buffer;
    struct sockaddr_in client_addr = data->client_addr;
    socklen_t client_addr_len = data->client_addr_len;
    int connection_sockfd = data->sockfd;

    printf(CYAN "Processing DHCP message from client %s:%d\n" RESET, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    dhcp_message_t dhcp_msg;

    // Parsear el mensaje recibido
    if (parse_dhcp_message((uint8_t*)buffer, &dhcp_msg) != 0) {
        printf(RED "Failed to parse DHCP message.\n" RESET);
        free(data);
        return NULL;
    }

    print_dhcp_message(&dhcp_msg, 0);

    uint8_t dhcp_message_type = 0;
    for (int i = 0; i < DHCP_OPTIONS_LENGTH; i++) {
        if (dhcp_msg.options[i] == 53) {
            dhcp_message_type = dhcp_msg.options[i + 2];
            break;
        }
    }

    switch (dhcp_message_type) {
        case DHCP_DISCOVER:
            printf(GREEN "Received DHCP_DISCOVER from client.\n" RESET);
            send_dhcp_offer(connection_sockfd, &client_addr, &dhcp_msg);
            break;
        case DHCP_REQUEST:
            printf(GREEN "Received DHCP_REQUEST from client.\n" RESET);
            handle_dhcp_request(connection_sockfd, &client_addr, &dhcp_msg);
            break;
        case DHCP_RELEASE:
            printf(GREEN "Received DHCP_RELEASE from client.\n" RESET);
            handle_dhcp_release(connection_sockfd, &dhcp_msg);
            break;
        default:
            printf(RED "Unrecognized DHCP message type: %d\n" RESET, dhcp_message_type);
            break;
    }

    free(data);
    return NULL;
}


void send_dhcp_offer(int socket_fd, struct sockaddr_in* client_addr, dhcp_message_t* discover_message) {
    dhcp_message_t offer_message;
    init_dhcp_message(&offer_message);
    memcpy(offer_message.chaddr, discover_message->chaddr, 6);

    char* assigned_ip = assign_ip();
    if (assigned_ip == NULL) {
        printf(RED "No available IP addresses in the pool.\n" RESET);
        set_dhcp_message_type(&offer_message, DHCP_NAK);
    } else {
        inet_pton(AF_INET, assigned_ip, &offer_message.yiaddr);
        inet_pton(AF_INET, server_ip, &offer_message.siaddr);
        inet_pton(AF_INET, gateway_ip, &offer_message.giaddr);
        set_dhcp_message_type(&offer_message, DHCP_OFFER);
        offer_message.options[3] = 1;
        offer_message.options[4] = 4;
        inet_pton(AF_INET, global_subnet_mask, &offer_message.options[5]);
        offer_message.options[9] = 6;
        offer_message.options[10] = 4;
        inet_pton(AF_INET, global_dns_ip, &offer_message.options[11]);
        offer_message.options[15] = 51;
        offer_message.options[16] = 4;
        uint32_t lease_time = htonl(LEASE_TIME);
        memcpy(&offer_message.options[17], &lease_time, 4);
        offer_message.options[21] = 255;
    }

    if (sendto(sockfd, &offer_message, sizeof(offer_message), 0, (struct sockaddr *) client_addr, sizeof(*client_addr)) < 0) {
        perror(RED "Error sending DHCP message" RESET);
    } else if (offer_message.options[2] == DHCP_OFFER) {
        printf(GREEN "DHCP_OFFER sent to client.\n" RESET);
    } else if (offer_message.options[2] == DHCP_NAK) {
        printf(RED "DHCP_NAK sent to client: IP not available.\n" RESET);
    }
}

void handle_dhcp_request(int sockfd, struct sockaddr_in* client_addr, dhcp_message_t* request_msg) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(request_msg->yiaddr), ip_str, INET_ADDRSTRLEN);

    renew_lease(ip_str);
    printf(GREEN "Sending DHCP_ACK for IP: %s\n" RESET, ip_str);
    set_dhcp_message_type(request_msg, DHCP_ACK);

    if (sendto(sockfd, request_msg, sizeof(*request_msg), 0, (struct sockaddr*)client_addr, sizeof(*client_addr)) < 0) {
        perror(RED "Error sending DHCP_ACK" RESET);
    }
}

void handle_dhcp_release(int sockfd, dhcp_message_t* release_msg) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(release_msg->ciaddr), ip_str, INET_ADDRSTRLEN);

    release_ip(ip_str);
    printf("IP address %s released.\n", ip_str);
}

void check_leases() {
    time_t current_time = time(NULL);
    for (int i = 1; i < pool_size; i++) {
        if (ip_pool[i].is_assigned) {
            if ((current_time - ip_pool[i].lease_start) >= ip_pool[i].lease_duration) {
                printf("Lease for IP %s has expired. Releasing IP...\n", ip_pool[i].ip_address);
                ip_pool[i].is_assigned = 0;
            }
        }
    }
}

void* check_and_release(void* arg) {
    while (1) {
        check_leases();
        sleep(1);
    }
}

// Implementación de la lógica principal del servidor

int main(int argc, char* argv[]) {
    srand(time(NULL));
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    socklen_t client_addr_len = sizeof(client_addr);

    printf(GREEN "Subnet loaded: %s\n" RESET, global_subnet_mask);
    printf(GREEN "Static DNS loaded: %s\n" RESET, global_dns_ip);

    signal(SIGINT, handle_signal_interrupt);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf(RED "Socket creation failed.\n" RESET);
        exit(0);
    }
    printf(GREEN "Socket created successfully.\n" RESET);

    generate_dynamic_gateway_ip(gateway_ip, sizeof(gateway_ip));
    printf(GREEN "Dynamic Gateway generated: %s\n" RESET, gateway_ip);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(68);

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf(RED "Socket bind failed.\n" RESET);
        close(sockfd);
        exit(0);
    }
    printf(GREEN "Socket bind successful.\n" RESET);
    printf(YELLOW "UDP server is running on %s:%d...\n" RESET, server_ip, port);

    // Inicializar el pool de direcciones IP
    char start_ip[16], end_ip[16];
    sscanf(ip_range, "%[^-]-%s", start_ip, end_ip);
    pool_size = calculate_pool_size(start_ip, end_ip);
    ip_pool = (ip_pool_entry_t*)malloc(pool_size * sizeof(ip_pool_entry_t));
    if (ip_pool == NULL) {
        printf("Failed to allocate memory for IP pool.\n");
        end_program();
    }

    unsigned int start = ip_to_int(start_ip);
    unsigned int end = ip_to_int(end_ip);
    for (unsigned int ip = start, i = 0; ip <= end; ip++, i++) {
        int_to_ip(ip, ip_pool[i].ip_address);
        ip_pool[i].is_assigned = 0;  // Inicialmente, todas las IP están desasignadas
    }

    pthread_t lease_thread;
    if (pthread_create(&lease_thread, NULL, check_and_release, NULL) != 0) {
        printf(RED "Failed to create leases thread.\n" RESET);
        end_program();
    }

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &client_addr_len);
        if (recv_len < 0) {
            printf(RED "Failed to receive data.\n" RESET);
            continue;
        }

        client_data_t* client_data = (client_data_t*)malloc(sizeof(client_data_t));
        if (!client_data) {
            printf(RED "Failed to allocate memory for client data.\n" RESET);
            continue;
        }

        client_data->sockfd = sockfd;
        memcpy(client_data->buffer, buffer, BUFFER_SIZE);
        client_data->client_addr = client_addr;
        client_data->client_addr_len = client_addr_len;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, process_client_connection, (void*)client_data) != 0) {
            printf(RED "Failed to create thread.\n" RESET);
            free(client_data);
            continue;
        }

        pthread_detach(thread_id);
    }

    end_program();
    return 0;
}

