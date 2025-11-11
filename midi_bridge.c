/**
 * Driver/Ponte MIDI de Usuário para Novation Ultranova (1235:0011)
 *
 * Versão 2.0 - Parser com Estado
 * - Suporta Running Status
 * - Suporta mensagens de 2 e 3 bytes (NoteOn/Off, CC, Pitch Bend, Prog Change, Aftertouch)
 *
 * Compilação:
 * gcc midi_bridge.c -o midi_bridge -lusb-1.0 -lasound
 */

#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>
#include <alsa/asoundlib.h> // Para ALSA (libasound)

// --- Configurações LibUSB ---
#define NOVATION_VID 0x1235
#define ULTRANOVA_PID 0x0011
#define MIDI_IN_ENDPOINT 0x83
#define INTERFACE_NUM 0

// --- Globais do ALSA ---
snd_seq_t *seq_handle; // Handle para o sequenciador ALSA
int alsa_port;         // ID da nossa porta MIDI virtual

// --- Globais do Parser MIDI (v2.0 - Máquina de Estado) ---
static unsigned char running_status = 0; // O último comando MIDI (ex: 0x90)
static unsigned char msg_buffer[3];      // Buffer para montar a mensagem
static int bytes_to_expect = 0;          // Quantos bytes de DADOS ainda esperamos (0, 1 ou 2)
static int msg_pos = 0;                  // Onde estamos no buffer

/**
 * Inicializa o Sequenciador ALSA e cria nossa porta MIDI virtual
 * (Esta é a função que estava faltando e causando o erro de 'linkagem')
 */
int setup_alsa_midi(void) {
    // Abre o sequenciador ALSA
    if (snd_seq_open(&seq_handle, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        fprintf(stderr, "Erro: Não foi possível abrir o sequenciador ALSA.\n");
        return -1;
    }
    
    // Define o nome do nosso "cliente" (nosso driver)
    snd_seq_set_client_name(seq_handle, "Ultranova Driver");

    // Cria a porta MIDI virtual
    alsa_port = snd_seq_create_simple_port(
        seq_handle,
        "Ultranova", // Nome da porta
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, // Pode ser lido
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION
    );

    if (alsa_port < 0) {
        fprintf(stderr, "Erro: Não foi possível criar a porta MIDI virtual.\n");
        snd_seq_close(seq_handle);
        return -1;
    }
    printf("Porta MIDI virtual 'Ultranova' criada no ALSA (Porta %d).\n", alsa_port);
    return 0;
}

/**
 * Envia uma mensagem MIDI completa (2 ou 3 bytes) para o ALSA
 */
void send_to_alsa(unsigned char* msg, int length) {
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);

    snd_seq_ev_set_source(&ev, alsa_port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);

    unsigned char command = msg[0] & 0xF0; // Ex: 0x90, 0xC0
    unsigned char channel = msg[0] & 0x0F;

    // Mensagens de 3 bytes
    if (length == 3) {
        if (command == 0x90 && msg[2] > 0) { // Note On
            ev.type = SND_SEQ_EVENT_NOTEON;
            ev.data.note.channel = channel;
            ev.data.note.note = msg[1];
            ev.data.note.velocity = msg[2];
        } else if (command == 0x80 || (command == 0x90 && msg[2] == 0)) { // Note Off
            ev.type = SND_SEQ_EVENT_NOTEOFF;
            ev.data.note.channel = channel;
            ev.data.note.note = msg[1];
            ev.data.note.velocity = 0;
        } else if (command == 0xB0) { // Control Change (CC)
            ev.type = SND_SEQ_EVENT_CONTROLLER;
            ev.data.control.channel = channel;
            ev.data.control.param = msg[1];
            ev.data.control.value = msg[2];
        } else if (command == 0xE0) { // Pitch Bend (implementado!)
            ev.type = SND_SEQ_EVENT_PITCHBEND;
            ev.data.control.channel = channel;
            // Pitch bend é 14 bits (2 bytes de dados)
            ev.data.control.value = (msg[2] << 7) | msg[1]; 
            // O valor do ALSA é de -8192 a 8191. O MIDI é 0-16383.
            // Precisamos subtrair o "centro" (8192)
            ev.data.control.value -= 8192;
        } else {
            return; // Ignora outras mensagens de 3 bytes por enquanto
        }
    }
    // Mensagens de 2 bytes
    else if (length == 2) {
        if (command == 0xC0) { // Program Change (implementado!)
            ev.type = SND_SEQ_EVENT_PGMCHANGE;
            ev.data.control.channel = channel;
            ev.data.control.value = msg[1]; // Número do programa
        } else if (command == 0xD0) { // Channel Aftertouch (implementado!)
            ev.type = SND_SEQ_EVENT_CHANPRESS;
            ev.data.control.channel = channel;
            ev.data.control.value = msg[1]; // Valor da pressão
        } else {
            return; // Ignora outras mensagens de 2 bytes
        }
    } else {
        return; // Ignora mensagens de tamanho inesperado
    }

    // Envia o evento
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle);
}

/**
 * O Parser MIDI v2.0 (Máquina de Estado).
 * Lida com mensagens de 2 e 3 bytes e "Running Status".
 */
void parse_midi_byte(unsigned char byte) {
    int is_status_byte = (byte & 0x80); // O primeiro bit é 1?

    if (is_status_byte) {
        // --- É um Byte de Status (Novo Comando) ---
        
        // Ignora mensagens de "System Real-Time" (0xF8-0xFF)
        if (byte >= 0xF8) {
            return; 
        }
        
        // (SysEx 0xF0 e 0xF7 serão ignorados por enquanto)

        // Salva como o novo "Running Status"
        running_status = byte;
        msg_pos = 1; // Posição 0 já está preenchida
        msg_buffer[0] = byte;

        // Decide quantos bytes de DADOS esperar
        unsigned char command = byte & 0xF0;
        if (command == 0xC0 || command == 0xD0) {
            bytes_to_expect = 1; // Espera 1 byte de dados (total 2)
        } else if (command == 0x80 || command == 0x90 || command == 0xB0 || command == 0xE0) {
            bytes_to_expect = 2; // Espera 2 bytes de dados (total 3)
        } else {
            // SysEx ou outros comandos que não tratamos
            bytes_to_expect = 0; 
            msg_pos = 0;
            running_status = 0;
        }

    } else {
        // --- É um Byte de Dados (Primeiro bit é 0) ---

        // 1. Aplicar "Running Status"?
        // Se recebemos um byte de DADOS, mas esperávamos um COMANDO (bytes_to_expect == 0)
        // E temos um "running_status" salvo...
        if (bytes_to_expect == 0 && running_status != 0) {
            
            // Reativa o "running status" como se o comando tivesse sido enviado
            parse_midi_byte(running_status); 
            
            // E então, processa o byte atual novamente
            parse_midi_byte(byte); 
            return; // O processamento continua nas chamadas recursivas
        }

        // 2. Coletar dados
        // Se estamos ativamente esperando por dados...
        if (bytes_to_expect > 0) {
            msg_buffer[msg_pos] = byte;
            msg_pos++;
            bytes_to_expect--;

            // 3. Mensagem Completa?
            if (bytes_to_expect == 0) {
                // Mensagem pronta! (seja de 2 ou 3 bytes)
                int total_length = msg_pos;
                printf("ALSA <- MIDI: ");
                for(int i=0; i<total_length; i++) printf("0x%02X ", msg_buffer[i]);
                printf("\n");

                send_to_alsa(msg_buffer, total_length);
                
                // Reseta para a próxima mensagem (mas mantém o running_status)
                msg_pos = 0; 
                // bytes_to_expect já é 0
            }
        }
    }
}

/**
 * Função principal: Loop do LibUSB
 */
void poll_usb(libusb_device_handle *handle) {
    unsigned char buffer[64];
    int actual_length;
    int r;

    printf("Ouvindo o Ultranova... Pressione Ctrl+C para sair.\n");
    while (1) {
        r = libusb_interrupt_transfer(
            handle,               // Nosso dispositivo
            MIDI_IN_ENDPOINT,     // O endpoint "IN" (0x83)
            buffer,               // Onde armazenar os dados
            sizeof(buffer),       // Tamanho máximo do buffer
            &actual_length,       // Onde o libusb dirá quantos bytes ele leu
            1000                  // Timeout em milissegundos (1s)
        );

        if (r == LIBUSB_SUCCESS) {
            // Sucesso! Passa cada byte recebido para o parser
            for (int i = 0; i < actual_length; i++) {
                parse_midi_byte(buffer[i]);
            }
        } else if (r == LIBUSB_ERROR_TIMEOUT) {
            // Timeout é normal, nada a fazer
        } else {
            // Erro real
            fprintf(stderr, "Erro na transferência USB: %s\n", libusb_error_name(r));
            return; // Sai da função de polling
        }
    }
}

// --- MAIN ---
int main() {
    libusb_device_handle *handle = NULL;
    int r;

    // 1. Inicializa ALSA
    if (setup_alsa_midi() != 0) {
        return 1;
    }

    // 2. Inicializa LibUSB
    r = libusb_init(NULL);
    if (r < 0) {
        fprintf(stderr, "Erro ao inicializar libusb.\n");
        return 1;
    }

    // 3. Abre o dispositivo
    handle = libusb_open_device_with_vid_pid(NULL, NOVATION_VID, ULTRANOVA_PID);
    if (handle == NULL) {
        fprintf(stderr, "Erro: Não foi possível encontrar o Ultranova (1235:0011).\n");
        fprintf(stderr, "Ele está conectado? Se sim, a regra udev falhou?\n");
        libusb_exit(NULL);
        return 1;
    }
    printf("Ultranova encontrado e aberto.\n");

    // 4. Reivindica a interface
    libusb_set_auto_detach_kernel_driver(handle, 1);
    r = libusb_claim_interface(handle, INTERFACE_NUM); 
    if (r < 0) {
        fprintf(stderr, "Erro ao reivindicar interface %d: %s\n", INTERFACE_NUM, libusb_error_name(r));
        if (r == LIBUSB_ERROR_ACCESS) {
            fprintf(stderr, "ERRO: Permissão negada. A regra udev está instalada e funcionando?\n");
        }
        libusb_close(handle);
        libusb_exit(NULL);
        return 1;
    }
    printf("Interface USB %d reivindicada. Ponte iniciada.\n", INTERFACE_NUM);

    // 5. Inicia o loop principal
    poll_usb(handle);

    // 6. Limpeza (se o loop quebrar, ex: Ctrl+C)
    printf("\nSaindo... Liberando interface e fechando.\n");
    libusb_release_interface(handle, INTERFACE_NUM);
    libusb_close(handle);
    libusb_exit(NULL);
    snd_seq_close(seq_handle);

    return 0;
}