#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <time.h>
#include <stddef.h>  // Incluido para offsetof
#include <sys/ioctl.h>  // Necesario para ioctl y SIOCGIFHWADDR


#define MAX_CHARACTERS 360
#define BUFFER_SIZE 1024
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
#define LEASE_TIME 3600  // Definido como 1 hora (3600 segundos)
#define RESET "\033[0m"
#define BOLD "\033[1m"
#define BLUE "\033[34m"
#define CYAN "\033[36m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define MAGENTA "\033[35m"
#define RED "\033[31m"

// Definir los valores directamente
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

int sockfd;
dhcp_message_t assigned_values_msg;
struct sockaddr_in server_addr;
int is_ip_assigned = 0;
int renewal_time;
time_t lease_start_time;
pthread_mutex_t ip_assignment_mutex = PTHREAD_MUTEX_INITIALIZER;

void send_dhcp_release(int sockfd, struct sockaddr_in* server_addr);
void end_program();
void handle_signal_interrupt(int signal);
void send_dhcp_request(int sockfd, struct sockaddr_in* server_addr, dhcp_message_t* msg);
void handle_dhcp_offer(int sockfd, struct sockaddr_in* server_addr, dhcp_message_t* msg);
void renew_lease_client(int sockfd, struct sockaddr_in* server_addr);
void* dhcp_listener(void* arg);

void init_dhcp_message(dhcp_message_t* msg) {
    memset(msg, 0, sizeof(dhcp_message_t));
    msg->op = 1; // BOOTREQUEST
    msg->htype = 1; // Ethernet
    msg->hlen = 6; // MAC address length
    msg->hops = 0;
    msg->xid = rand(); // Transaction ID
    msg->secs = 0;
    msg->flags = htons(0x8000); // Broadcast flag
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

int set_dhcp_message_type(dhcp_message_t* msg, uint8_t type) {
    if (!msg)
        return -1;
    if (sizeof(msg->options) < 3)
        return -1;
    msg->options[0] = 53;   // Option 53 is DHCP message type
    msg->options[1] = 1;    // Length of the DHCP message type option
    msg->options[2] = type; // Set the actual DHCP message type
    return 0;
}

void print_dhcp_message(const dhcp_message_t* msg, int is_client) {
    printf(BOLD BLUE "\n==================== DHCP MESSAGE ====================\n" RESET);

    printf(BOLD CYAN "Operation Code (op)     " RESET ": " GREEN "%d\n" RESET, msg->op);
    printf(BOLD CYAN "Transaction ID (xid)    " RESET ": " GREEN "0x%08X\n" RESET, msg->xid);

    struct in_addr client_ip;
    client_ip.s_addr = is_client ? htonl(msg->ciaddr) : msg->ciaddr;
    printf(BOLD CYAN "Client IP Address       " RESET ": " GREEN "%s\n" RESET, inet_ntoa(client_ip));

    struct in_addr your_ip;
    your_ip.s_addr = is_client ? htonl(msg->yiaddr) : msg->yiaddr;
    printf(BOLD CYAN "Offered IP (Your IP)    " RESET ": " GREEN "%s\n" RESET, inet_ntoa(your_ip));

    struct in_addr server_ip;
    server_ip.s_addr = is_client ? htonl(msg->siaddr) : msg->siaddr;
    printf(BOLD CYAN "Server IP Address       " RESET ": " GREEN "%s\n" RESET, inet_ntoa(server_ip));

    struct in_addr gateway_ip;
    gateway_ip.s_addr = is_client ? htonl(msg->giaddr) : msg->giaddr;
    printf(BOLD CYAN "Gateway IP Address      " RESET ": " GREEN "%s\n" RESET, inet_ntoa(gateway_ip));

    printf(BOLD CYAN "Client MAC Address      " RESET ": ");
    for (int i = 0; i < msg->hlen; i++) {
        printf(MAGENTA "%02x" RESET, msg->chaddr[i]);
        if (i < msg->hlen - 1)
            printf(":");
    }
    printf("\n");

    const uint8_t* options = msg->options;
    size_t options_length = sizeof(msg->options);
    size_t i = 0;

    while (i < options_length) {
        uint8_t option = options[i++];
        if (option == 255)
            break;
        uint8_t length = options[i++];

        if (option == 1 && length == 4) {
            printf(BOLD CYAN "Subnet Mask             " RESET ": " GREEN "%d.%d.%d.%d\n" RESET,
                   options[i], options[i + 1], options[i + 2], options[i + 3]);
        }

        if (option == 6 && length == 4) {
            printf(BOLD CYAN "DNS Server              " RESET ": " GREEN "%d.%d.%d.%d\n" RESET,
                   options[i], options[i + 1], options[i + 2], options[i + 3]);
        }
        i += length;
    }

    printf(BOLD YELLOW "\n==================== DHCP TYPE ====================\n" RESET);
}

void send_dhcp_request(int sockfd, struct sockaddr_in* server_addr, dhcp_message_t* msg) {
    set_dhcp_message_type(msg, DHCP_REQUEST);
    if (sendto(sockfd, msg, sizeof(*msg), 0, (struct sockaddr*)server_addr, sizeof(*server_addr)) < 0) {
        perror(RED "Error sending DHCP_REQUEST" RESET);
    } else {
        printf(CYAN "DHCP_REQUEST message sent to the server.\n" RESET);
    }
}

void handle_dhcp_offer(int sockfd, struct sockaddr_in* server_addr, dhcp_message_t* msg) {
    send_dhcp_request(sockfd, server_addr, msg);   
}

void send_dhcp_release(int sockfd, struct sockaddr_in* server_addr) {
    set_dhcp_message_type(&assigned_values_msg, DHCP_RELEASE);
    if (sendto(sockfd, &assigned_values_msg, sizeof(assigned_values_msg), 0, (struct sockaddr*)server_addr, sizeof(*server_addr)) < 0) {
        perror(RED "Error sending DHCP_RELEASE" RESET);
    } else {
        printf(CYAN "DHCP_RELEASE message sent to the server.\n" RESET);
    }
}

void end_program() {
    if (is_ip_assigned) {
        send_dhcp_release(sockfd, &server_addr);
    } else {
        printf("No IP address assigned. Skipping DHCP_RELEASE.\n");
    }

    if (sockfd >= 0) {
        close(sockfd);
    }

    printf(MAGENTA "Exiting...\n" RESET);
    exit(0);
}

void handle_signal_interrupt(int signal) {
    printf(YELLOW "\nSignal %d received.\n" RESET, signal);
    end_program();
}

void renew_lease_client(int sockfd, struct sockaddr_in* server_addr) {
    printf("Renewing lease...\n");

    if (!is_ip_assigned) {
        printf(RED "No IP address assigned. Skipping lease renewal.\n" RESET);
        return;
    }

    send_dhcp_request(sockfd, server_addr, &assigned_values_msg);
    lease_start_time = time(NULL);
}

void* dhcp_listener(void* arg) {
    socklen_t addr_len = sizeof(server_addr);
    char buffer[BUFFER_SIZE];
    dhcp_message_t msg;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int recv_len = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, &addr_len);
        if (recv_len < 0) {
            printf(RED "Failed to receive data.\n" RESET);
            continue;
        }

        if (parse_dhcp_message((uint8_t*)buffer, &msg) != 0) {
            printf(RED "Failed to parse received message.\n" RESET);
            continue;
        }

        print_dhcp_message(&msg, 1);
        uint8_t dhcp_message_type = 0;
        for (int i = 0; i < DHCP_OPTIONS_LENGTH; i++) {
            if (msg.options[i] == 53) {
                dhcp_message_type = msg.options[i + 2];
                break;
            }
        }

        switch (dhcp_message_type) {
            case DHCP_OFFER:
                pthread_mutex_lock(&ip_assignment_mutex);
                printf(GREEN "DHCP_OFFER received. Offered IP: %s\n" RESET, inet_ntoa(*(struct in_addr*)&msg.yiaddr));
                handle_dhcp_offer(sockfd, &server_addr, &msg);
                pthread_mutex_unlock(&ip_assignment_mutex);
                break;
            case DHCP_ACK:
                pthread_mutex_lock(&ip_assignment_mutex);
                printf(GREEN "DHCP_ACK received. Assigned IP: %s\n" RESET, inet_ntoa(*(struct in_addr*)&msg.yiaddr));
                assigned_values_msg = msg;
                assigned_values_msg.ciaddr = msg.yiaddr;
                is_ip_assigned = 1;
                lease_start_time = time(NULL);
                pthread_mutex_unlock(&ip_assignment_mutex);
                break;
            case DHCP_NAK:
                printf(RED "DHCP_NAK received: The IP was not assigned.\n" RESET);
                is_ip_assigned = 0;
                break;
            default:
                printf(RED "Unrecognized DHCP message type: %d\n" RESET, dhcp_message_type);
                break;
        }
    }

    return NULL;
}

int build_dhcp_message(const dhcp_message_t* msg, uint8_t* buffer, size_t buffer_size) {
    if (buffer_size < sizeof(dhcp_message_t))
        return -1;

    memset(buffer, 0, buffer_size);
    memcpy(buffer, msg, sizeof(dhcp_message_t));

    uint32_t* xid_ptr = (uint32_t*)((char*)buffer + offsetof(dhcp_message_t, xid));
    *xid_ptr = htonl(msg->xid);

    uint16_t* secs_ptr = (uint16_t*)((char*)buffer + offsetof(dhcp_message_t, secs));
    *secs_ptr = htons(msg->secs);

    uint16_t* flags_ptr = (uint16_t*)((char*)buffer + offsetof(dhcp_message_t, flags));
    *flags_ptr = htons(msg->flags);

    buffer[offsetof(dhcp_message_t, options) + 3] = 255;  // Option 255 marks the end

    return 0;
}

int get_mac_address(uint8_t* mac, const char* iface) {
    struct ifreq ifr;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        perror("Socket creation failed");
        return -1;
    }

    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);  // Nombre de la interfaz (e.g., "eth0")
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {    // Obtener la dirección MAC
        perror("Failed to get MAC address");
        close(fd);
        return -1;
    }

    close(fd);

    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);  // Copiar la dirección MAC
    return 0;
}

int main() {
    socklen_t addr_len = sizeof(server_addr);
    dhcp_message_t msg;
    char buffer[BUFFER_SIZE];

    // Registrar el manejador de señal para SIGINT (CTRL+C)
    signal(SIGINT, handle_signal_interrupt);

    // Inicializar el tiempo de renovación de la IP
    renewal_time = LEASE_TIME / 2;

    // Crear el socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);  // AF_INET: IPv4, SOCK_DGRAM: UDP
    if (sockfd < 0) {
        printf(RED "Socket creation failed.\n" RESET);
        exit(0);
    }
    printf(GREEN "Socket created successfully.\n" RESET);

    // Asociar el socket al puerto 68 del cliente
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Escuchar en cualquier interfaz de red
    client_addr.sin_port = htons(68);  // Puerto 68 para el cliente

    // Hacer bind del socket al puerto 68
    if (bind(sockfd, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        perror("Failed to bind client to port 68");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Habilitar el broadcast para el socket
    int enable_broadcast = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &enable_broadcast, sizeof(enable_broadcast)) < 0) {
        perror("Error setting SO_BROADCAST");
        close(sockfd);
        return -1;
    }

    // Configurar la dirección del servidor DHCP
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    if (strlen(server_ip) == 0) {
        printf("Server IP not provided. Using broadcast address.\n");
        server_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);  // Usar broadcast si no se proporciona la IP
    } else {
        printf("Using server IP: %s\n", server_ip);
        server_addr.sin_addr.s_addr = inet_addr(server_ip);  // Usar la IP del servidor proporcionada
    }
    server_addr.sin_port = htons(67);  // Usar el puerto del servidor DHCP (67)

    // Crear un hilo para escuchar las respuestas DHCP (OFFER/ACK)
    pthread_t listener_thread;
    if (pthread_create(&listener_thread, NULL, dhcp_listener, NULL) != 0) {
        printf(RED "Failed to create listener thread.\n" RESET);
        end_program();
    }

    // Iniciar el tiempo de descubrimiento para DHCP Discover
    time_t start_discover_time = time(NULL);

    // Bucle principal
    while (1) {
        time_t current_discover_time = time(NULL);

        pthread_mutex_lock(&ip_assignment_mutex);
        if (current_discover_time - start_discover_time >= 10 && is_ip_assigned == 0) {
            // Inicializar el mensaje DHCP Discover
            init_dhcp_message(&msg);
            set_dhcp_message_type(&msg, DHCP_DISCOVER);

            // Obtener la MAC address del cliente e incluirla en el mensaje DHCP Discover
            const char* iface = "enp0s3";  // Interfaz de red (puedes cambiar esto)
            if (get_mac_address(msg.chaddr, iface) == 0) {
                msg.hlen = 6;  // Longitud de la dirección MAC
            } else {
                printf(RED "Failed to get MAC address.\n" RESET);
                close(sockfd);
                return -1;
            }

            // Serializar y enviar el mensaje DHCP Discover
            build_dhcp_message(&msg, (uint8_t*)buffer, sizeof(buffer));
            int sent_bytes = sendto(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, addr_len);
            if (sent_bytes < 0) {
                printf(RED "Failed to send message to server.\n" RESET);
                close(sockfd);
                return -1;
            }

            printf(CYAN "DHCP Discover message sent to server.\n" RESET);
            start_discover_time = time(NULL);
        } else if (is_ip_assigned) {
            // Renovar la IP después de la mitad del tiempo de lease
            time_t current_time = time(NULL);
            int time_elapsed = (int)difftime(current_time, lease_start_time);

            if (time_elapsed >= renewal_time) {
                renew_lease_client(sockfd, &server_addr);
            }
        }
        pthread_mutex_unlock(&ip_assignment_mutex);

        sleep(1);
    }

    // Finalizar el programa
    end_program();
    return 0;
}
