/* UARTCLI-based demo application providing a CLI for switching the LEDs, reading the VLOCLK TimerA
 * tweaking the rate of TimerA in relation to VLOCLK.
 */

#include <msp430.h>
#include <stdlib.h>
#include <string.h>
#include "uartcli.h"

const char *cmdlist[] = {"led", "vlo", "vloirq", "vlorate", "help", NULL};

/* VLOCLK timer user status */
volatile char vlostat;
#define VLO_ENABLED 0x01
#define VLO_IRQ_MARK 0x02

/* Function prototypes */
void VLO_TimerA_switch(char);
char VLO_TimerA_enabled();
void VLO_TimerA_set_divider(char);
char VLO_TimerA_get_divider();
void VLO_TimerA_overflow_mark(char);
char VLO_TimerA_overflow_mark_enabled();
void VLO_TimerA_overflow_check();
void command_processor();


/* Main loop */
int main()
{
        char cmd[64];

        WDTCTL = WDTPW | WDTHOLD;
        DCOCTL = CALDCO_16MHZ;
        BCSCTL1 = CALBC1_16MHZ;
        BCSCTL2 = DIVS_2;  // SMCLK = MCLK/4
        BCSCTL3 = LFXT1S_2;  // ACLK = VLOCLK/1
        while (BCSCTL3 & LFXT1OF)
                ;

        /* Global initialization */
        vlostat = 0x00;
        P1OUT &= ~(BIT0 | BIT6);  // LEDs switched off, enabled as GPIO outputs
        P1DIR |= (BIT0 | BIT6);   //
        P1SEL &= ~(BIT0 | BIT6);  //
        P1SEL2 &= ~(BIT0 | BIT6); //

        // Let'er rip
        uartcli_begin(cmd, 32);
        uartcli_println_str("vlotoy MSP430 Command Line Interface");
        uartcli_println_str("OK");

        while (1) {
                if (uartcli_available()) {
                        command_processor();
                        uartcli_clear();  // Tell UART code we're done processing the last cmd, and it can accept new input.
                                          // Otherwise it will quite literally ignore every character coming in.
                        uartcli_println_str("OK");
                }
                if (vlostat & VLO_IRQ_MARK) {
                        uartcli_println_str("VLOCLK MARK");
                        vlostat &= ~VLO_IRQ_MARK;
                }
                LPM0;
        }
}

void VLO_TimerA_switch(char onoff)
{
        if (onoff) {
                TACTL = (TACTL & ID_3) |  // Save previous value of input divider
                        (TACTL & TAIE) |  // Save previous value of TAIE, but clear TAIFG
                        TASSEL_1 |      // ACLK (VLOCLK)
                        MC_2 |          // Count continuously
                        TACLR;          // Clear timer
                vlostat |= VLO_ENABLED;
        } else {
                TACTL &= ~MC_3;         // Stop mode
                vlostat &= ~VLO_ENABLED;
        }
}

char VLO_TimerA_enabled()
{
        if ( (TACTL & MC_3) != 0x00 )
                return 1;
        return 0;
}

void VLO_TimerA_set_divider(char divider)
{
        switch(divider) {
                case 1:
                        TACTL = (TACTL & ~ID_3);
                        break;
                case 2:
                        TACTL = (TACTL & ~ID_3) | ID_1;
                        break;
                case 4:
                        TACTL = (TACTL & ~ID_3) | ID_2;
                        break;
                case 8:
                        TACTL |= ID_3;
                        break;
        }
}

char VLO_TimerA_get_divider()
{
        switch (TACTL & ID_3) {
                case ID_0:
                        return 1;
                        break;
                case ID_1:
                        return 2;
                        break;
                case ID_2:
                        return 4;
                        break;
                case ID_3:
                        return 8;
                        break;
        }
        return 0;  // should never reach this far
}

void VLO_TimerA_overflow_mark(char onoff)
{
        if (onoff) {
                TACTL &= ~TAIFG;
                TACTL |= TAIE;
        } else {
                TACTL &= ~(TAIFG | TAIE);
        }
}

char VLO_TimerA_overflow_mark_enabled()
{
        if (TACTL & TAIE)
                return 1;
        return 0;
}

void VLO_TimerA_overflow_check()
{
        if (vlostat & VLO_IRQ_MARK) {
                uartcli_print_str("VLOCLK mark");
                vlostat &= ~VLO_IRQ_MARK;
        }
}

/* TimerA overflow interrupt */
#pragma vector=TIMER0_A1_VECTOR
__interrupt void TA_ISR(void)
{
        TACTL &= ~TAIFG;
        vlostat |= VLO_IRQ_MARK;
        __bic_SR_register_on_exit(LPM4_bits);
}

/* Command processor */
// const char *cmdlist[] = {"led", "vlo", "vloirq", "vlorate", "help", NULL};
void command_processor()
{
        int cmdidx, i=0;
        char buf[32];

        uartcli_token_begin();
        cmdidx = uartcli_token_cmd(cmdlist);
        switch (cmdidx) {
                case 0:  // LED
                        if (uartcli_token_arg(1, buf, 32) != NULL) {
                                i = 0;
                                if (!strcmp(buf, "on")) {
                                        P1OUT |= BIT0;  // Enable red LED
                                        uartcli_println_str("led: switched on");
                                        i = 1;
                                }
                                if (!strcmp(buf, "off")) {
                                        P1OUT &= ~BIT0; // Disable red LED
                                        uartcli_println_str("led: switched off");
                                        i = 1;
                                }
                                if (!i) {
                                        uartcli_println_str("Syntax: led <on/off>");
                                }
                        } else {
                                // Without any arguments, we assume the user is querying the status.
                                if (P1OUT & BIT0) {
                                        uartcli_println_str("led: on");
                                } else {
                                        uartcli_println_str("led: off");
                                }
                        }
                        break;
                case 1:  // VLO enable/disable
                        if (uartcli_token_arg(1, buf, 32) != NULL) {
                                if (!strcmp(buf, "on")) {
                                        VLO_TimerA_switch(1);
                                        uartcli_print_str("VLOCLK switched on, timer divider = ");
                                        uartcli_println_int(VLO_TimerA_get_divider());
                                }
                                if (!strcmp(buf, "off")) {
                                        VLO_TimerA_switch(0);
                                        uartcli_println_str("VLOCLK switched off");
                                }
                        } else {
                                // Display VLO TimerA on/off status
                                if (VLO_TimerA_enabled()) {
                                        uartcli_print_str("VLOCLK Timer enabled; current value: ");
                                        uartcli_println_uint(TAR);
                                } else {
                                        uartcli_println_str("VLOCLK Timer disabled");
                                }
                        }
                        break;
                case 2:  // VLOIRQ (overflow mark) enable/disable
                        if (uartcli_token_arg(1, buf, 32) != NULL) {
                                if (!strcmp(buf, "on")) {
                                        VLO_TimerA_overflow_mark(1);
                                        uartcli_println_str("VLOCLK overflow IRQ messages enabled");
                                }
                                if (!strcmp(buf, "off")) {
                                        VLO_TimerA_overflow_mark(0);
                                        uartcli_println_str("VLOCLK overflow IRQ messages disabled");
                                }
                        } else {
                                // Display VLO overflow mark status
                                if (VLO_TimerA_overflow_mark_enabled()) {
                                        uartcli_println_str("VLOCLK overflow IRQ messages: enabled");
                                } else {
                                        uartcli_println_str("VLOCLK overflow IRQ messages: disabled");
                                }
                        }
                        break;
                case 3:  // VLORATE (set divider)
                        if (vlostat & VLO_ENABLED) {
                                if (uartcli_token_arg(1, buf, 32) != NULL) {
                                        i = atoi(buf);
                                        if (i != 1 && i != 2 && i != 4 && i != 8) {
                                                uartcli_print_str("vlorate: Inappropriate divider value: ");
                                                uartcli_println_str(buf);
                                                uartcli_println_str("Divider must be 1, 2, 4 or 8.");
                                        } else {
                                                // Set it!
                                                VLO_TimerA_set_divider(i);
                                                uartcli_print_str("VLOCLK timer divider set to: ");
                                                uartcli_println_int(i);
                                        }
                                } else {
                                        // Display current VLO TimerA divider
                                        uartcli_print_str("VLOCLK timer divider set to: ");
                                        uartcli_println_int(VLO_TimerA_get_divider());
                                }
                        } else {
                                uartcli_println_str("VLOCLK timer disabled; ignoring command");
                        }
                        break;
                case 4:  // HELP
                        uartcli_println_str("HELP -- Command reference");
                        uartcli_println_str("led     : Query red LED status or switch LED on/off");
                        uartcli_println_str("vlo     : Query runtime status of VLOCLK-enabled TimerA or switch on/off");
                        uartcli_println_str("vloirq  : Display whether 16-bit timer overflow IRQ marks are enabled or switch on/off");
                        uartcli_println_str("vlorate : Display or set the TimerA clock divider");
                        break;
                default:
                        uartcli_print_str("Unknown command: ");
                        uartcli_token_cmdstr(buf, 32);
                        uartcli_println_str(buf);
        }
}
