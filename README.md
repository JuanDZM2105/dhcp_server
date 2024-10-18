# Proyecto DHCP - Cliente y Servidor en C

## Introducción
Este proyecto implementa un **servidor y cliente DHCP** escritos en C. El servidor tiene la capacidad de asignar direcciones 
IP de manera dinámica a los clientes que lo solicitan, además de gestionar el arrendamiento (lease) de las direcciones IP, incluyendo
la renovación y liberación de estas. El cliente puede solicitar, renovar y liberar direcciones IP durante su ejecución.

El protocolo DHCP es esencial para la asignación dinámica de configuraciones IP en redes, lo que permite a los dispositivos 
obtener automáticamente los parámetros necesarios para conectarse a una red sin necesidad de configuraciones manuales. Este 
proyecto proporciona una implementación básica de un sistema DHCP utilizando sockets UDP en C.

## Desarrollo
### Estructura del Proyecto
El proyecto se compone de dos módulos principales:
- **Cliente DHCP**: Envío de solicitudes DHCP al servidor, procesamiento de las respuestas (dirección IP, máscara, DNS, gateway),
renovación y liberación de IP.
- **Servidor DHCP**: Recepción de solicitudes, asignación dinámica de direcciones IP desde un pool predefinido, y gestión
 del arrendamiento de estas direcciones IP.

### Funcionalidades del Cliente
- **DHCPDISCOVER**: Envío de una solicitud para obtener una dirección IP.
- **DHCPOFFER**: Recepción de una oferta con dirección IP y otros parámetros de red.
- **DHCPREQUEST**: Solicitud para confirmar la asignación de la dirección IP ofrecida.
- **DHCPRENEW**: Solicitud para renovar el tiempo de arrendamiento de la IP antes de que expire.
- **DHCPRELEASE**: Liberación de la dirección IP asignada.

### Funcionalidades del Servidor
- **Asignación de direcciones IP**: Gestión dinámica de un rango de IPs disponibles para los clientes.
- **Gestión del arrendamiento (lease)**: Control del tiempo de asignación de las direcciones IP.
- **Renovación y liberación de IPs**: Soporte para renovar el arrendamiento y liberar direcciones IP cuando ya no son necesarias.
- **Manejo concurrente**: Uso de hilos (`pthread`) para permitir que el servidor maneje múltiples solicitudes de clientes
simultáneamente.

## Aspectos Logrados y No Logrados
### Aspectos Logrados
- El servidor DHCP escucha correctamente las solicitudes de los clientes y asigna direcciones IP dinámicamente desde un rango configurado.
- El cliente DHCP es capaz de solicitar, recibir, renovar y liberar direcciones IP.
- El servidor gestiona correctamente el tiempo de arrendamiento de las IP, permitiendo la renovación y liberación de estas.
- El servidor soporta múltiples clientes de manera simultánea gracias al uso de hilos.
- El cliente libera la dirección IP automáticamente al terminar la ejecución.

### Aspectos No Logrados
- **Soporte para DHCP Relay**: El servidor no está configurado para manejar solicitudes de subredes diferentes a la suya mediante un
DHCP relay. Esta funcionalidad es importante en redes más complejas donde los clientes están en diferentes subredes que el servidor DHCP.
- **Manejo de NAK y DECLINE**: Actualmente el servidor no soporta el envío de mensajes NAK (Negative Acknowledgment) o DECLINE, que
son útiles para manejar errores o rechazos de solicitudes de IP.

## Conclusiones
Este proyecto logra implementar un sistema básico de asignación dinámica de direcciones IP usando el protocolo DHCP. Se cumple
con la asignación y gestión de direcciones IP en una red local, permitiendo a los clientes obtener y gestionar sus 
configuraciones de red de manera automática. A través del uso de hilos, el servidor DHCP es capaz de manejar múltiples clientes 
de manera concurrente, haciendo que sea adecuado para entornos simples.

Para mejorar el proyecto, sería recomendable añadir soporte para DHCP Relay y gestionar adecuadamente mensajes de error y rechazo, 
como `NAK` y `DECLINE`. Estas características harían el servidor más robusto y adecuado para entornos más complejos.

## Referencias

