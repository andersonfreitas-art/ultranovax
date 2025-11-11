/**
 * Driver/Ponte MIDI de Usuário para Novation Ultranova (1235:0011)
 *
 * Este programa lê dados MIDI brutos do endpoint USB do Ultranova usando libusb
 * e os retransmite para uma porta MIDI virtual do ALSA.
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

// --- Globais do Parser MIDI ---
unsigned char midi_msg[3]; // Buffer para montar a mensagem MIDI (trata apenas de msgs de 3 bytes)
int msg_pos = 0;           // Quantos bytes já recebemos para a msg atual

/**
 * Inicializa o Sequenciador ALSA e cria nossa porta MIDI virtual
 */
int setup_alsa_midi() {
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
 * Envia uma mensagem MIDI completa para o ALSA
 * (Versão inteligente que entende os eventos)
 */
void send_to_alsa(unsigned char b1, unsigned char b2, unsigned char b3) {
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev); // Limpa o evento

    // Configura a origem (nossa porta) e o destino (qualquer um ouvindo)
    snd_seq_ev_set_source(&ev, alsa_port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev); // Envia direto

    // --- A nova lógica inteligente ---
    unsigned char command = b1 & 0xF0; // Pega o comando (ex: 0x90, 0x80, 0xB0)
    unsigned char channel = b1 & 0x0F; // Pega o canal (0-15)

    if (command == 0x90 && b3 > 0) {
        // --- Note On ---
        // (b3 > 0 porque Note On com velocidade 0 é um Note Off)
        ev.type = SND_SEQ_EVENT_NOTEON;
        ev.data.note.channel = channel;
        ev.data.note.note = b2;
        ev.data.note.velocity = b3;
    } 
    else if (command == 0x80 || (command == 0x90 && b3 == 0)) {
        // --- Note Off ---
        // (Tanto 0x80 quanto 0x90 com vel 0)
        ev.type = SND_SEQ_EVENT_NOTEOFF;
        ev.data.note.channel = channel;
        ev.data.note.note = b2;
        ev.data.note.velocity = 0; // Velocidade do Note Off
    }
    else if (command == 0xB0) {
        // --- Control Change (CC) --- (Para seus Knobs!)
        ev.type = SND_SEQ_EVENT_CONTROLLER;
        ev.data.control.channel = channel;
        ev.data.control.param = b2; // Número do CC
        ev.data.control.value = b3; // Valor do CC
    }
    else {
        // Outros eventos (Pitch Bend, etc.) que ainda não tratamos
        // Por enquanto, não fazemos nada para evitar logs
        return;
    }

    // Envia o evento (agora estruturado corretamente)
    snd_seq_event_output(seq_handle, &ev);
    snd_seq_drain_output(seq_handle); // Envia "agora"
}

/**
 * O Parser MIDI. Recebe bytes um por um e monta a mensagem.
 * Lida com pacotes USB fragmentados.
 */
void parse_midi_byte(unsigned char byte) {
    // Byte de Status (começa com 1, ex: 0x91, 0x81)
    if (byte & 0x80) {
        msg_pos = 0; // Reinicia a montagem da mensagem
        midi_msg[msg_pos++] = byte;
    } 
    // Byte de Dado (começa com 0) e estamos no meio de uma mensagem
    else if (msg_pos > 0) {
        midi_msg[msg_pos++] = byte;
    }

    // Se temos 3 bytes, a mensagem está completa!
    // (Este é um parser simples que SÓ trata mensagens de 3 bytes)
    if (msg_pos == 3) {
        printf("ALSA <- MIDI: 0x%02X 0x%02X 0x%02X\n", midi_msg[0], midi_msg[1], midi_msg[2]);
        send_to_alsa(midi_msg[0], midi_msg[1], midi_msg[2]);
        msg_pos = 0; // Reseta para a próxima mensagem
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
            // Timeout é normal, significa que não houve dados
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