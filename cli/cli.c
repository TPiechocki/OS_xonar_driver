//
// Created by Tomasz Piechocki on 22/12/2020.
//

#include <stdio.h>
#include <alsa/asoundlib.h>
#include <alsa/mixer.h>

#define clear() printf("\033[H\033[J")

#define CARD_NUMBER 0
#define VOL_INDEX 1
#define MUTE_INDEX 2
#define FRONT_INDEX 3

// structures to perform control operations
snd_mixer_t *handle;
snd_mixer_selem_id_t *sid;
snd_mixer_elem_t* elem;

/**
 * Show and optionally set the mute switch
 */
void mute_controller() {
    while (1) {
        // get current state
        int ch0, ch1;
        snd_mixer_selem_get_playback_switch(elem, 0, &ch0);
        snd_mixer_selem_get_playback_switch(elem, 0, &ch1);
        // and print it
        printf("Stan Front (0=wyciszony): %d\t\t%d\n", ch0, ch1);

        // give possibility to set
        printf("Podaj '0' jeśli chcesz wyciszyć, '1' żeby odciszyć lub 'q' by wyjść.\n");
        char answer;
        answer = getchar();
        getchar(); // newline symbol
        switch (answer) {
            case '0':
                // mute
                snd_mixer_selem_set_playback_switch_all(elem, 0);
                break;
            case '1':
                // unmute
                snd_mixer_selem_set_playback_switch_all(elem, 1);
                break;
            case 'q':
                return;
            default:
                printf("Zła opcja, spróbuj ponownie.\n");
        }
    }
}

/**
 * Show volume controls and optionally possibility to set them.
 */
void vol_controller() {
    while (1) {
        // get min and max values of this control
        long min, max;
        snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        // and print them
        clear();
        printf("Zakres możliwych głośności: %ld-%ld\n", min, max);

        // get current values of front outputs
        long ch0, ch1;
        snd_mixer_selem_get_playback_volume(elem, 0, &ch0);
        snd_mixer_selem_get_playback_volume(elem, 0, &ch1);
        // and print them
        printf("Poziom głośności wyjść Front: %ld\t\t%ld\n", ch0, ch1);

        // give possibility to set
        printf("Podaj '0', jeśli chcesz ustawić nowy poziom lub 'q' by wyjść.\n");
        char answer;
        answer = getchar();
        getchar(); // newline symbol
        switch (answer) {
            case '0':
                printf("Podaj liczbę: ");
                int level;
                scanf("%d", &level);
                getchar(); // newline symbol
                // set if numbers are in correct range
                if (level >= min && level <= max)
                    snd_mixer_selem_set_playback_volume_all(elem, level);
                else
                    printf("Zła wartość.");
                break;
            case 'q':
                return;
            default:
                printf("Zła opcja, spróbuj ponownie.\n");
        }
    }
}

/** TODO
 * Show front panel switch and optionally possibility to change its state.
 */
void front_panel_controller() {
    while (1) {
        int ch0;
        // get current state of the switch
        snd_mixer_selem_get_playback_switch(elem, 0, &ch0);
        // and print it
        printf("Wyjście na przedni panel, gdy 1: %d\n", ch0);

        // give possibility to set
        printf("Podaj '0' jeśli dźwięk ma przechodzić normalnie przez kartę,\n"
               "'1' żeby dźwięk szedł do przedniego panelu lub 'q' by wyjść.\n");
        char answer;
        answer = getchar();
        getchar(); // newline symbol
        switch (answer) {
            case '0':
                snd_mixer_selem_set_playback_switch_all(elem, 0);
                break;
            case '1':
                snd_mixer_selem_set_playback_switch_all(elem, 1);
                break;
            case 'q':
                return;
            default:
                printf("Zła opcja, spróbuj ponownie.\n");
        }
    }
}

int main() {
    // allocate alsa mixer structures
    snd_mixer_open(&handle, 0);
    // assumption that card is set as default
    snd_mixer_attach(handle, "default");
    snd_mixer_selem_register(handle, NULL, NULL);
    // load the mixer for the flag
    snd_mixer_load(handle);

    // find the element of the mixer for required controls
    // name can be find using amixer
    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_name(sid,"Master");

    // open the element of the mixer
    elem = snd_mixer_find_selem(handle, sid);


    while (1) {
        printf("Witaj w CLI!\n"
               "Wybierz funkcję do ustawienia lub 'q', jeśli chcesz wyjść.:\n"
               "1) Głośności\t\t2) Wyciszenia\t\t3) Przełącznika przedniego panelu\n");

        char answer;
        answer = getchar();
        getchar(); // newline symbol

        switch (answer) {
            case '1':
                vol_controller();
                break;
            case '2':
                mute_controller();
                break;
            case '3':
                front_panel_controller();
                break;
            case 'q':
                return 0;
            default:
                printf("Zła opcja, spróbuj ponownie.");
        }
    }

    return 0;
}