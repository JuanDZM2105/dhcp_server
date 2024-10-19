# Proyecto: Servidor y Cliente DHCP

## Índice
1. [Integrantes del grupo](#integrantes-del-grupo)
2. [Link del video de la sustentación](#link-del-video)
3. [Introducción](#introducción)
4. [Desarrollo](#desarrollo)
5. [Aspectos logrados y no logrados](#aspectos-logrados-y-no-logrados)
6. [Conclusiones](#conclusiones)
7. [Referencias](#referencias)
8. [Instrucciones para compilar y ejecutar](#instrucciones-para-compilar-y-ejecutar)

---

## Integrantes del grupo
- **Anderson Sebastian Jimenez Mercado**  
- **Felipe Martinez Cortez**  
- **Juan David Zapata**

---

## Link del video
[Enlace al video de la sustentación](https://eafit-my.sharepoint.com/:f:/g/personal/asjimenezm_eafit_edu_co/EoWAX6YuPzVPnXisgqTfZV8BOTqloysMw_v3YDyLJOkxNw?e=JlFJtJ)

---

## Introducción
En este proyecto, implementamos un **servidor DHCP** utilizando sockets en lenguaje C y un **cliente DHCP**. El objetivo es permitir la asignación dinámica de direcciones IP a clientes en una red. El servidor es capaz de manejar múltiples solicitudes concurrentes, gestionar el arrendamiento de IPs, y trabajar con relés DHCP para asignaciones en subredes diferentes.

---

## Desarrollo
### Servidor DHCP
El servidor está implementado en C y utiliza la API de **Berkeley Sockets**. Soporta mensajes DHCP DISCOVER, REQUEST, RELEASE y RENEW. El servidor asigna direcciones IP dinámicamente desde un pool predefinido y maneja el arrendamiento de las mismas.

### Cliente DHCP
El cliente puede ser ejecutado en cualquier sistema y se comunica con el servidor mediante sockets. Envía mensajes DISCOVER para obtener una dirección IP, y puede solicitar la renovación o liberación de la IP asignada.

---

## Aspectos Logrados y No logrados
### Logrados
- Implementación de los mensajes **DHCPDISCOVER**, **DHCPREQUEST**, **DHCPRELEASE** y **DHCPRENEW**.
- Gestión concurrente de solicitudes utilizando **hilos**.
- Funcionamiento correcto del servidor con **relés DHCP** para subredes.
  
### No logrados
- Implementación parcial de algunos mensajes opcionales como **NAK** y **DECLINE**.

---

## Conclusiones
La implementación del servidor y cliente DHCP permite asignar y gestionar direcciones IP dinámicamente en una red. El uso de sockets y el manejo de concurrencia con hilos proporcionan una solución eficiente para la administración de direcciones IP en redes locales y remotas.

---

## Referencias
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [RFC 2131 - DHCP](https://datatracker.ietf.org/doc/html/rfc2131)
- [TCP Server/Client Implementation in C](https://www.geeksforgeeks.org/tcp-server-client-implementation-in-c/)

---

## Instrucciones para compilar y ejecutar

### Servidor DHCP

#### Requisitos:
- Sistema operativo Linux
- GCC (GNU Compiler Collection)
- Acceso a privilegios de root (puertos 67 y 68 son reservados)

#### Pasos:

1. **Compilación**:
   Para compilar el servidor DHCP, abre una terminal y ejecuta el siguiente comando:
   ```bash
   gcc -o dhcp_server dhcp_server.c -lpthread
