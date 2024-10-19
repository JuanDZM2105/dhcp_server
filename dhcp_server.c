#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <time.h>

#define LEASE_DURATION 60  // Lease time in seconds (2 minutes)
#define SERVER_PORT 67
#define MAX_MESSAGE_SIZE 2048
#define AVAILABLE_IPS 10
#define MAX_CONCURRENT_THREADS 4
#define DNS "8.8.8.8"
#define SUBNET_MASK "255.255.255.0"

char SERVER_GATEWAY[NI_MAXHOST];
char *ip_pool[AVAILABLE_IPS];
int ip_status[AVAILABLE_IPS] = {0};
time_t lease_expiration[AVAILABLE_IPS];
int active_connections = 0;
pthread_mutex_t thread_lock = PTHREAD_MUTEX_INITIALIZER;

void obtain_gateway_ip() {
    struct ifaddrs *network_interfaces, *interface;
    char ip_address[NI_MAXHOST];

    if (getifaddrs(&network_interfaces) == -1) {
        perror("Error fetching network interfaces");
        exit(EXIT_FAILURE);
    }

    for (interface = network_interfaces; interface != NULL; interface = interface->ifa_next) {
        if (interface->ifa_addr == NULL) continue;

        if (interface->ifa_addr->sa_family == AF_INET) {
            if (getnameinfo(interface->ifa_addr, sizeof(struct sockaddr_in), ip_address, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                if (strcmp(interface->ifa_name, "lo") != 0) {
                    strncpy(SERVER_GATEWAY, ip_address, sizeof(SERVER_GATEWAY));
                    break;
                }
            }
        }
    }

    freeifaddrs(network_interfaces);
}

void create_ip_range(char *ip_pool[], const char *start_ip, const char *end_ip) {
    int start_segments[4], end_segments[4];
    sscanf(start_ip, "%d.%d.%d.%d", &start_segments[0], &start_segments[1], &start_segments[2], &start_segments[3]);
    sscanf(end_ip, "%d.%d.%d.%d", &end_segments[0], &end_segments[1], &end_segments[2], &end_segments[3]);

    int index = 0;
    for (int i = start_segments[2]; i <= end_segments[2]; i++) {
        for (int j = start_segments[3]; j <= end_segments[3]; j++) {
            if (index >= AVAILABLE_IPS) return;
            snprintf(ip_pool[index], 16, "%d.%d.%d.%d", start_segments[0], start_segments[1], i, j);
            index++;
        }
    }
}

void process_ip_release(int client_socket, struct sockaddr_in *client_address, socklen_t address_length, const char *ip_to_release) {
    for (int i = 0; i < AVAILABLE_IPS; i++) {
        if (strcmp(ip_pool[i], ip_to_release) == 0 && ip_status[i] == 1) {
            ip_status[i] = 0;
            lease_expiration[i] = 0; // Resetear el tiempo de arrendamiento
            printf("Released IP: %s\n", ip_pool[i]);

            char message[256];
            snprintf(message, sizeof(message), "IP %s released successfully", ip_pool[i]);
            sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)client_address, address_length);
            return;
        }
    }
    printf("IP not in pool: %s\n", ip_to_release);
    char error_message[256];
    snprintf(error_message, sizeof(error_message), "Error: IP not found");
    sendto(client_socket, error_message, strlen(error_message), 0, (struct sockaddr *)client_address, address_length);
}

void process_ip_renewal(int client_socket, struct sockaddr_in *client_address, socklen_t address_length, const char *ip_to_renew) {
    for (int i = 0; i < AVAILABLE_IPS; i++) {
        if (strcmp(ip_pool[i], ip_to_renew) == 0 && ip_status[i] == 1) {
            lease_expiration[i] = time(NULL) + LEASE_DURATION; // Renovar el tiempo de arrendamiento
            printf("IP %s renewed.\n", ip_to_renew);

            char message[256];
            snprintf(message, sizeof(message), "IP %s renewed successfully. Lease time: %d seconds", ip_to_renew, LEASE_DURATION);
            sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)client_address, address_length);
            return;
        }
    }
    printf("IP not found or not allocated: %s\n", ip_to_renew);
    char error_message[256];
    snprintf(error_message, sizeof(error_message), "Error: IP not found or allocated");
    sendto(client_socket, error_message, strlen(error_message), 0, (struct sockaddr *)client_address, address_length);
}

void process_ip_request(int client_socket, struct sockaddr_in *client_address, socklen_t address_length, const char *requested_ip) {
    for (int i = 0; i < AVAILABLE_IPS; i++) {
        if (strcmp(ip_pool[i], requested_ip) == 0 && ip_status[i] == 0) {
            ip_status[i] = 1;
            lease_expiration[i] = time(NULL);
            char message[256];
            snprintf(message, sizeof(message), "Acknowledged: IP %s assigned", requested_ip);
            sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)client_address, address_length);
            printf("Assigned IP: %s\n", requested_ip);
            return;
        }
    }
    char retry_message[256];
    snprintf(retry_message, sizeof(retry_message), "Retry: Invalid IP");
    sendto(client_socket, retry_message, strlen(retry_message), 0, (struct sockaddr *)client_address, address_length);
    printf("Invalid IP request. Retry initiated.\n");
}

void process_ip_discovery(int client_socket, struct sockaddr_in *client_address, socklen_t address_length) {
    char *available_ip = NULL;
    time_t current_time = time(NULL);
    char message[2048];  // Aumentamos el tamaño del buffer a 2048

    for (int i = 0; i < AVAILABLE_IPS; i++) {
        if (ip_status[i] == 0) {
            available_ip = ip_pool[i];
            lease_expiration[i] = current_time;
            break;
        }
    }

    if (available_ip != NULL) {
        snprintf(message, sizeof(message),
                 "Offered IP: %s\n"
                 "Mask: %s\n"
                 "DNS: %s\n"
                 "Gateway: %s\n"
                 "Lease time: %d seconds",
                 available_ip,
                 SUBNET_MASK, 
                 DNS, 
                 SERVER_GATEWAY, 
                 LEASE_DURATION);

        sendto(client_socket, message, strlen(message), 0, (struct sockaddr *)client_address, address_length);
        printf("Offered IP: %s\n", available_ip);
    } else {
        char *no_ip_message = "No available IPs.";
        sendto(client_socket, no_ip_message, strlen(no_ip_message), 0, (struct sockaddr *)client_address, address_length);
        printf("No available IPs to offer.\n");
    }
}

void *client_handler(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[MAX_MESSAGE_SIZE];
    struct sockaddr_in client_address;
    socklen_t address_length = sizeof(client_address);
    int message_size;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        message_size = recvfrom(client_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_address, &address_length);

        if (message_size < 0) {
            perror("Communication error");
            continue;
        }

        printf("Received: %s\n", buffer);

        if (strcmp(buffer, "DHCPDISCOVER") == 0) {
            process_ip_discovery(client_socket, &client_address, address_length);
        } else if (strstr(buffer, "DHCPREQUEST") != NULL) {
            char *ip_request = strtok(buffer + strlen("DHCPREQUEST: "), " ");
            if (ip_request != NULL) {
                process_ip_request(client_socket, &client_address, address_length, ip_request);
            }
        } else if (strstr(buffer, "DHCPRELEASE") != NULL) {
            char *ip_release = strtok(buffer + strlen("DHCPRELEASE: "), " ");
            if (ip_release != NULL) {
                process_ip_release(client_socket, &client_address, address_length, ip_release);
            }
        } else if (strstr(buffer, "DHCPRENEW") != NULL) {
            char *ip_renew = strtok(buffer + strlen("DHCPRENEW: "), " ");
            if (ip_renew != NULL) {
                process_ip_renewal(client_socket, &client_address, address_length, ip_renew);
            }
        }
    }

    close(client_socket);
    pthread_mutex_lock(&thread_lock);
    active_connections--;
    pthread_mutex_unlock(&thread_lock);

    return NULL;
}

int main() {
    obtain_gateway_ip();
    printf("Gateway IP: %s\n", SERVER_GATEWAY);

    int server_socket;
    struct sockaddr_in server_address;

    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0) {
        printf("Failed to create socket.\n");
        exit(EXIT_FAILURE);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        printf("Socket binding failed.\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < AVAILABLE_IPS; i++) {
        ip_pool[i] = malloc(16 * sizeof(char));
    }
    create_ip_range(ip_pool, "192.168.0.1", "192.168.0.10");

    printf("DHCP server running on port %d...\n", SERVER_PORT);

    while (1) {
        pthread_mutex_lock(&thread_lock);
        if (active_connections < MAX_CONCURRENT_THREADS) {
            pthread_mutex_unlock(&thread_lock);

            int *client_socket = malloc(sizeof(int));
            *client_socket = server_socket;

            pthread_t thread;
            if (pthread_create(&thread, NULL, client_handler, client_socket) != 0) {
                perror("Failed to create thread");
                free(client_socket);
            } else {
                pthread_detach(thread);
                pthread_mutex_lock(&thread_lock);
                active_connections++;
                pthread_mutex_unlock(&thread_lock);
            }
        } else {
            pthread_mutex_unlock(&thread_lock);
            sleep(1);
        }
    }

    close(server_socket);
    return 0;
}
