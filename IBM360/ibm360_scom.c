/* ibm360_scom.c: IBM 360 3271 scommunications controller

   Copyright (c) 2017-2020, Richard Cornwell

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


*/

#include "ibm360_defs.h"
#include "sim_sock.h"
#include "sim_tmxr.h"

#ifdef NUM_DEVS_COM
#define UNIT_COM           UNIT_DISABLE



/* u3 */
#define CMD_WR             0x01       /* Write data to com line */
#define CMD_RD             0x02       /* Read buffer */
#define CMD_NOP            0x03       /* Nop scommand */
#define CMD_WRER           0x05       /* Erase and write data */
#define CMD_RDMD           0x06       /* Read modified */
#define CMD_SEL            0x0B       /* Select */
#define CMD_WRERALT        0x0D       /* Write erase alternative */
#define CMD_EAU            0x0F       /* Erase all un protected */
#define CMD_WSF            0x11       /* Write structured field */
#define CMD_SNSID          0xE4       /* Sense ID */

/* u4 second byte */
#define RECV               0x00100    /* Recieving data */
#define SEND               0x00200    /* Sending data */
#define ENAB               0x00400    /* Line enabled */
#define DATA               0x00800    /* Data available */
#define INIT1              0x01000    /* Send DO EOR, waiting WILL EOR */
#define INPUT              0x02000    /* Input ready */
#define ATTN               0x04000    /* Send attention signal */
#define HALT               0x08000    /* Halt operation */
#define CONN               0x10000    /* Device connected */

#define NEG_DONE           0          /* Negotiation done */
#define NEG_TERM           1          /* Terminal option sent */
#define NEG_TERM_TYPE      2          /* Terminal type sent */
#define NEG_EOR            3          /* EOR Sent */
#define NEG_BINARY         4          /* Binary sent */


/* Upper 11 bits of u3 hold the device address */

/* u4 */
/* Where we are reading from */

/* u5 */
/* Sense byte 0 */
/* u6 */
/* Pointer into buffer */

#define CMD        u3
#define STATUS     u4
#define SNS        u5
#define BPTR       u6

#define TC_WILL    0x1                 /* Option in will state */
#define TC_WONT    0x2                 /* Wont do option */
#define TC_DO      0x4                 /* Will do option. */
#define TC_DONT    0x8                 /* Dont do option  */

#define EOR        239                 /* End of record */
#define SE         240                 /* End of negotiation */
#define BREAK      243                 /* Break */
#define IP         244                 /* Interrupt pending */
#define SB         250                 /* Subnegotation */
#define WILL       251                 /* I will use this option */
#define WONT       252                 /* I wont use this option */
#define DO         253                 /* Use this option */
#define DONT       254                 /* Dont use option */
#define IAC        255                 /* Interpret as command */


/* Telnet options we care about */
#define OPTION_BINARY    0             /* Send 8 bit data */
#define OPTION_ECHO      1             /* Echo */
#define OPTION_SGA       3             /* Set go ahead */
#define OPTION_TERMINAL  24            /* Request terminal type */
#define OPTION_EOR       25            /* Handle end of record */

#define TS_DATA      0                 /* Regular state */
#define TS_IAC       1                 /* Have seen IAC */
#define TS_WILL      2                 /* Have seen IAC WILL */
#define TS_WONT      3                 /* Have seen IAC WONT */
#define TS_DO        4                 /* Have seen IAC DO */
#define TS_DONT      5                 /* Have seen IAC DONT */
#define TS_SB        6                 /* Have seen SB */

/* Remote orders */
#define REMOTE_EAU      0x6F           /* Erase all unprotected */
#define REMOTE_EW       0xF5           /* Erase/Write */
#define REMOTE_RB       0xF2           /* Read Buffer */
#define REMOTE_RM       0x6e           /* Read Modified */
#define REMOTE_WRERALT  0x7e           /* Write erase alternative */
#define REMOTE_WRT      0xF1           /* Write */
#define REMOTE_WSF      0xF3           /* Write structured field */

struct _line {
    uint16        option_state[256];   /* Current telnet state */
    uint8         state;               /* Current line status */
    char          term_type[50];       /* Current terminal type */
} line_data[NUM_UNITS_SCOM];

extern int32     tmxr_poll;

uint8       scoml_startcmd(UNIT *uptr, uint8 cmd) ;
uint8       scoml_haltio(UNIT *uptr);
t_stat      scoml_srv(UNIT *uptr);
t_stat      scom_reset(DEVICE *dptr);
t_stat      scom_scan(UNIT *uptr);
t_stat      scom_readinput(UNIT *uptr);
void        scom_sendoption(UNIT *uptr, int unit, uint8 state, uint8 opt);
t_stat      scom_attach(UNIT *uptr, CONST char *);
t_stat      scom_detach(UNIT *uptr);
t_stat      scom_help (FILE *, DEVICE *, UNIT *, int32, const char *);
const char *scom_description (DEVICE *);

uint8       scom_buf[NUM_UNITS_SCOM][256];
TMLN        scom_ldsc[NUM_UNITS_SCOM];
TMXR        scom_desc = { NUM_UNITS_SCOM, 0, 0, scom_ldsc};


MTAB                scom_mod[] = {
    {0}
};

MTAB                scoml_mod[] = {
    {MTAB_XTD | MTAB_VUN | MTAB_VALR, 0, "DEV", "DEV", &set_dev_addr,
        &show_dev_addr, NULL},
    {0}
};

UNIT                scom_unit[] = {
    {UDATA(&scom_scan, UNIT_ATTABLE | UNIT_DISABLE | UNIT_IDLE, 0)},        /* Line scanner */
};

UNIT                scoml_unit[] = {
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x050)},       /* 0 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x051)},       /* 1 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x052)},       /* 2 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x053)},       /* 3 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x054)},       /* 4 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x055)},       /* 5 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x056)},       /* 6 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x057)},       /* 7 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x058)},       /* 8 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x059)},       /* 9 */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x05A)},       /* A */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x05B)},       /* B */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x05C)},       /* C */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x05D)},       /* D */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x05E)},       /* E */
    {UDATA(&scoml_srv, UNIT_COM, 0), 0, UNIT_ADDR(0x05F)},       /* F */
};

struct dib scom_dib = { 0xF8, NUM_UNITS_SCOM, NULL, scoml_startcmd,
    scoml_haltio, scoml_unit, NULL};

DEVICE              scom_dev = {
    "SCOM", scom_unit, NULL, scom_mod,
    NUM_DEVS_SCOM, 8, 15, 1, 8, 8,
    NULL, NULL, scom_reset, NULL, &scom_attach, &scom_detach,
    NULL, DEV_MUX | DEV_DISABLE | DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &scom_help, NULL, NULL, &scom_description
};

DEVICE              scoml_dev = {
    "SCOML", scoml_unit, NULL, scoml_mod,
    NUM_UNITS_SCOM, 8, 15, 1, 8, 8,
    NULL, NULL, NULL, NULL, NULL, NULL,
    &scom_dib, DEV_DISABLE | DEV_DEBUG, 0, dev_debug
};


/*
 * Issue a command to the 3271 controller.
 */
uint8  scoml_startcmd(UNIT *uptr,  uint8 cmd) {
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);

    sim_debug(DEBUG_CMD, dptr, "CMD unit=%d %x\n", unit, cmd);
    if ((uptr->CMD & 0xff) != 0) {
       return SNS_BSY;
    }

    if (scom_ldsc[unit].conn == 0 && cmd != CMD_SENSE) {
       uptr->SNS = SNS_INTVENT;
       return SNS_UNITCHK;
    }

    if (cmd == 0)
         return 0;

    switch (cmd) {
    case CMD_WR:            /* Write data to com line */
    case CMD_RD:            /* Read buffer */
    case CMD_WRER:          /* Erase and write data */
    case CMD_RDMD:          /* Read modified */
    case CMD_WRERALT:       /* Write erase alternative */
    case CMD_WSF:           /* Write structured field */
    case CMD_SNSID:         /* Sense ID */
         uptr->SNS = 0;
         uptr->CMD |= cmd;
         sim_activate(uptr, 200);
         return 0;

    case CMD_NOP:           /* Nop scommand */
         break;

    case CMD_SENSE:
         uptr->CMD |= cmd;
         sim_activate(uptr, 20);
         return 0;


    case CMD_SEL:           /* Select */
    case CMD_EAU:           /* Erase all un protected */
         uptr->SNS = 0;
         uptr->CMD |= cmd;
         sim_activate(uptr, 200);
         return SNS_CHNEND;

    case 0:
         return 0;

    default:
         uptr->SNS = SNS_CMDREJ;
         break;
    }
    if (uptr->SNS & 0xff)
        return SNS_CHNEND|SNS_DEVEND|SNS_UNITCHK;
    return SNS_CHNEND|SNS_DEVEND;
}

/*
 * Handle halt I/O instruction by stoping running scommand.
 */
uint8  scoml_haltio(UNIT *uptr) {
    uint16         addr = GET_UADDR(uptr->CMD);
    DEVICE         *dptr = find_dev_from_unit(uptr);
    int            unit = (uptr - dptr->units);
    int            cmd = uptr->CMD & 0xff;

    sim_debug(DEBUG_CMD, dptr, "HLTIO unit=%d %x\n", unit, cmd);
    if ((scom_unit[0].flags & UNIT_ATT) == 0)              /* attached? */
        return 3;

    switch (cmd) {
    case 0:
    case 0x4:
    case CMD_SNSID:      /* Sense ID */
    case CMD_SEL:        /* Select */
    case CMD_NOP:        /* Nop scommand */
         /* Short scommands nothing to do */
         break;

    case CMD_WR:         /* Write data to com line */
    case CMD_RD:         /* Read buffer */
    case CMD_WRER:       /* Erase and write data */
    case CMD_RDMD:       /* Read modified */
    case CMD_EAU:        /* Erase all un protected */
    case CMD_WSF:        /* Write Structured field */
    case CMD_WRERALT:    /* Write erase alternative */
         uptr->CMD |= HALT;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         sim_activate(uptr, 20);
         break;
    }
    return 1;
}

/* Handle per unit scommands */
t_stat scoml_srv(UNIT * uptr)
{
    uint16              addr = GET_UADDR(uptr->CMD);
    DEVICE             *dptr = find_dev_from_unit(uptr);
    int                 unit = (uptr - dptr->units);
    int                 cmd = uptr->CMD & 0xff;
    uint8               ch;

    if (scom_ldsc[unit].conn == 0 && cmd != CMD_SENSE) {
       uptr->SNS = SNS_INTVENT;
       set_devattn(addr, SNS_UNITCHK);
       return SCPE_OK;
    }

    if ((uptr->STATUS & (RECV|DATA|INIT1)) != 0) {
        sim_activate(uptr, 200);
        return scom_readinput(uptr);
    }

    switch (cmd) {
    case CMD_NOP:
         uptr->CMD &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_SENSE:
         ch = uptr->SNS & 0xff;
         if (scom_ldsc[unit].conn == 0) {
            uptr->SNS |= SNS_INTVENT;
         }
         sim_debug(DEBUG_DETAIL, dptr, "sense unit=%d 1 %x\n", unit, ch);
         chan_write_byte(addr, &ch) ;
         uptr->CMD &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_SNSID:      /* Sense ID */
         sim_debug(DEBUG_DETAIL, dptr, "sense id=%d\n", unit);
         ch = 0xFF;
         chan_write_byte(addr, &ch) ;
         ch = 0x32;
         chan_write_byte(addr, &ch) ;
         ch = 0x74;
         chan_write_byte(addr, &ch) ;
         ch = 0x1B;
         chan_write_byte(addr, &ch) ;
         uptr->CMD &= ~0xff;
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;

    case CMD_RDMD:       /* Read modified */
    case CMD_RD:         /* Read in data from scom line */
         uptr->SNS = 0;
         if ((uptr->STATUS & HALT) != 0) {
             uptr->CMD &= ~(0xff);
             uptr->STATUS &= ~(RECV);
             return SCPE_OK;
         }
         if ((uptr->STATUS & ENAB) != 0) {
             if ((uptr->STATUS & RECV) == 0) {
                  /* Send cmd IAC EOR */
                 if (tmxr_rqln(&scom_ldsc[unit]) == 0) {
                     sim_debug(DEBUG_DETAIL, dptr, "unit=%d Send read cmd %x\n",
                                  unit, cmd);
                     if (cmd == CMD_RD)
                         tmxr_putc_ln( &scom_ldsc[unit], REMOTE_RB);
                     else
                         tmxr_putc_ln( &scom_ldsc[unit], REMOTE_RM);
                     tmxr_putc_ln( &scom_ldsc[unit], IAC);
                     tmxr_putc_ln( &scom_ldsc[unit], EOR);
                 }
                 uptr->STATUS |= RECV;
             }
             sim_activate(uptr, 200);
         }
         break;


    case CMD_WRERALT:   /* Write erase alternative */
         ch = REMOTE_WRERALT;
         goto write;

    case CMD_WSF:       /* Write structured field */
         ch = REMOTE_WSF;
         goto write;

    case CMD_WRER:       /* Erase and write data */
         ch = REMOTE_EW;
         goto write;

    case CMD_EAU:        /* Erase all un protected */
         ch = REMOTE_EAU;
         goto write;

    case CMD_WR:         /* Write data to com line */
         ch = REMOTE_WRT;
write:
         uptr->SNS = 0;
         if ((uptr->STATUS & HALT) != 0) {
             uptr->CMD &= ~(0xFF);
             uptr->STATUS &= ~(SEND);
             return SCPE_OK;
         }
         if ((uptr->STATUS & ENAB) != 0) {
             if ((uptr->STATUS & SEND) == 0) {
                 sim_debug(DEBUG_DETAIL, dptr, "unit=%d send write %x\n", unit,
                         ch);
                 tmxr_putc_ln( &scom_ldsc[unit], ch);
                 uptr->STATUS |= SEND;
             }
             if (chan_read_byte (addr, &ch)) {
                 tmxr_putc_ln( &scom_ldsc[unit], IAC);
                 tmxr_putc_ln( &scom_ldsc[unit], EOR);
                 uptr->CMD &= ~(0xff);
                 uptr->STATUS &= ~(SEND);
                 sim_debug(DEBUG_CMD, dptr, "COM: unit=%d eor\n", unit);
                 chan_end(addr, SNS_CHNEND|SNS_DEVEND);
             } else {
                 int32 data;
                 data = ebcdic_to_ascii[ch];
                 sim_debug(DEBUG_CMD, dptr, "COM: unit=%d send %02x '%c'\n",
                              unit, ch, isprint(data)? data: '^');
                 tmxr_putc_ln( &scom_ldsc[unit], ch);
                 if (ch == IAC)
                     tmxr_putc_ln( &scom_ldsc[unit], ch);
                 sim_activate(uptr, 200);
             }
         }
         break;

//    case CMD_NOP:        /* Nop scommand */
//         break;

    case CMD_SEL:        /* Select */
         uptr->CMD &= ~0xff;
         uptr->SNS = 0;
         sim_debug(DEBUG_CMD, dptr, "COM: unit=%d select done\n", unit);
         chan_end(addr, SNS_CHNEND|SNS_DEVEND);
         break;
    }

    return SCPE_OK;
}

/* Scan for new connections, flush and poll for data */
t_stat scom_scan(UNIT * uptr)
{
    UNIT           *line;
    int32           ln;
    struct _line   *data;
    int             i;

    sim_activate(uptr, tmxr_poll);          /* continue poll */
    if ((uptr->flags & UNIT_ATT) == 0)              /* attached? */
        return SCPE_OK;
    ln = tmxr_poll_conn (&scom_desc);                 /* look for connect */
    sim_debug(DEBUG_EXP, &scom_dev, "SCOM Poll %d\n", ln);
    if (ln >= 0) {                                  /* got one? rcv enb*/
        line = &scoml_unit[ln];
        data = (struct _line *)(line->up7);
        sim_debug(DEBUG_DETAIL, &scom_dev, "SCOM line connect %d\n", ln);
        scom_ldsc[ln].rcve = 1;                 /* Mark as ok */
        for (i = 0; i < 256; i++)
            data->option_state[i] = 0;
        scom_sendoption(line, ln, DO, OPTION_TERMINAL);
        line->STATUS |= ENAB|DATA|INIT1|CONN;
        line->STATUS &= ~(RECV|SEND);
        line->SNS = 0;
        sim_activate(line, 2000);
    }

    /* See if a line is disconnected with no scommand on it. */
    for (ln = 0; ln < scom_desc.lines; ln++) {
        line = &scoml_unit[ln];
        if ((line->STATUS & (SEND|ENAB)) == ENAB &&
                  tmxr_rqln(&scom_ldsc[ln]) > 0) {
            set_devattn(GET_UADDR(line->CMD), SNS_ATTN);
        }
        if (scom_ldsc[ln].conn == 0 && (line->STATUS & CONN) != 0) {
            line->STATUS = 0;
            line->CMD &= ~0xff;
            sim_debug(DEBUG_DETAIL, &scom_dev, "set disconnect %d\n", ln);
            line->SNS |= SNS_INTVENT;
            set_devattn(GET_UADDR(line->CMD), SNS_ATTN);
        }
    }
    tmxr_poll_tx(&scom_desc);
    tmxr_poll_rx(&scom_desc);
    return SCPE_OK;
}

/* Process characters from remote */
t_stat
scom_readinput(UNIT *uptr)
{
    DEVICE             *dptr = find_dev_from_unit(uptr);
    uint16              addr = GET_UADDR(uptr->CMD);
    int                 unit = (uptr - dptr->units);
    int32               r;
    struct _line        *data = (struct _line *)(uptr->up7);
    uint8               ch;

    while (((r = tmxr_getc_ln (&scom_ldsc[unit])) & TMXR_VALID) != 0) {
        ch = r & 0xff;
        sim_debug(DEBUG_DETAIL, dptr, "unit=%d got %x\n", unit, ch);
        switch (data->state) {
        case TS_DATA:
             if (ch == IAC) {
                 data->state = TS_IAC;
                 break;
             }
             if (uptr->STATUS & RECV) {
                 sim_debug(DEBUG_DETAIL, dptr, "unit=%d sent %x\n", unit, ch);
                 if (chan_write_byte( addr, &ch)) {
                     uptr->CMD &= ~(0xff);
                     uptr->STATUS &= ~(RECV);
                     chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                     return SCPE_OK;
                 }
             }
             break;
        case TS_SB:
             if (ch == IAC) {
                 data->state = TS_IAC;
             } else {
                 data->term_type[uptr->BPTR++] = ch;
             }
             break;

        case TS_IAC:
             switch (ch) {
             case WILL:
                  data->state = TS_WILL;
                  break;

             case WONT:
                  data->state = TS_WONT;
                  break;

             case DO:
                  data->state = TS_DO;
                  break;

             case DONT:
                  data->state = TS_DONT;
                  break;

             case IAC:
                  data->state = TS_DATA;
                  if (uptr->STATUS & RECV) {
                      sim_debug(DEBUG_DETAIL, dptr, "unit=%d sent %x\n", unit,
                                  ch);
                      if (chan_write_byte( addr, &ch)) {
                          uptr->CMD &= ~(0xff);
                          uptr->STATUS &= ~(RECV);
                          chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                          return SCPE_OK;
                      }
                  }
                  break;

             case SB:
                  data->state = TS_SB;
                  sim_debug(DEBUG_DETAIL, dptr, "unit=%d SB\n", unit);
                  uptr->BPTR = 0;
                  break;

             case SE:
                  data->term_type[uptr->BPTR++] = '\0';
                  data->state = TS_DATA;
                  sim_debug(DEBUG_DETAIL, dptr, "unit=%d term = %s\n", unit,
                       &data->term_type[2]);
                  scom_sendoption(uptr, unit, WILL, OPTION_EOR);
                  /* Clear WILL flag, because we have not recieved on yet */
                  data->option_state[OPTION_EOR] &= ~TC_WILL;
                  scom_sendoption(uptr, unit, DO, OPTION_EOR);
                  break;

             case IP:
             case BREAK:
             case EOR:
                  data->state = TS_DATA;
                  if (uptr->STATUS & RECV) {
                      uptr->CMD &= ~(0xff);
                      uptr->STATUS &= ~(RECV);
                      chan_end(addr, SNS_CHNEND|SNS_DEVEND);
                  }
                  break;
             }
             break;

        case TS_WILL:
             switch (ch) {
             case OPTION_TERMINAL:   /* Ignore terminal option */
                  if ((data->option_state[ch] & TC_WILL) == 0) {
                      sim_debug(DEBUG_DETAIL, dptr,
                               "unit=%d Will terminal %d\n", unit, ch);
                      data->option_state[ch] |= TC_WILL;
                      tmxr_putc_ln( &scom_ldsc[unit], IAC);
                      tmxr_putc_ln( &scom_ldsc[unit], SB);
                      tmxr_putc_ln( &scom_ldsc[unit], OPTION_TERMINAL);
                      tmxr_putc_ln( &scom_ldsc[unit], 1);
                      tmxr_putc_ln( &scom_ldsc[unit], IAC);
                      tmxr_putc_ln( &scom_ldsc[unit], SE);
                      tmxr_send_buffered_data(&scom_ldsc[unit]);
                  }
                  break;

             case OPTION_EOR:
                  sim_debug(DEBUG_DETAIL, dptr, "unit=%d Will EOR %d %x\n",
                          unit, ch, data->option_state[ch]);
                  if ((data->option_state[ch] & TC_WILL) == 0) {
                      data->option_state[ch] |= TC_WILL;
                      scom_sendoption(uptr, unit, WILL, OPTION_BINARY);
                      data->option_state[OPTION_BINARY] &= ~TC_WILL;
                      scom_sendoption(uptr, unit, DO, OPTION_BINARY);
                  }
                  break;

             case OPTION_BINARY:
                  if ((data->option_state[ch] & TC_WILL) == 0) {
                      data->option_state[ch] |= TC_WILL;
                      tmxr_putc_ln( &scom_ldsc[unit], REMOTE_EW);
                      tmxr_putc_ln( &scom_ldsc[unit], 0xc1);
                      tmxr_putc_ln( &scom_ldsc[unit], IAC);
                      tmxr_putc_ln( &scom_ldsc[unit], EOR);
                      tmxr_send_buffered_data(&scom_ldsc[unit]);
                      if ((uptr->CMD & 0xff) == 0) {
                          sim_debug(DEBUG_DETAIL, dptr, "end arb %d\n", unit);
                          uptr->SNS = 0;
                          uptr->STATUS &= ~(DATA|INIT1);
                          set_devattn(addr, SNS_DEVEND);
                      } else {
                          sim_debug(DEBUG_DETAIL, dptr, "arb  %d\n", unit);
                      }
                  }
                  break;

             case OPTION_ECHO:
             case OPTION_SGA:
                  if ((data->option_state[ch] & TC_WILL) == 0) {
                      sim_debug(DEBUG_DETAIL, dptr, "unit=%d Will Binary %d\n",
                                    unit, ch);
                      data->option_state[ch] |= TC_WILL;
                      scom_sendoption(uptr, unit, DO, OPTION_EOR);
                  }
                  break;

             default:
                  sim_debug(DEBUG_DETAIL, dptr, "unit=%d Will default %d\n",
                                 unit, ch);
                  if ((data->option_state[ch] & TC_DONT) == 0)
                      scom_sendoption(uptr, unit, DONT, ch);
                  break;
             }
             data->state = TS_DATA;
             break;

        case TS_WONT:
             sim_debug(DEBUG_DETAIL, dptr, "unit=%d wont %d\n", unit, ch);
             if ((data->option_state[ch] & TC_WONT) == 0)
                 scom_sendoption(uptr, unit, WONT, ch);
             break;

        case TS_DO:
             switch (ch) {
             case OPTION_TERMINAL:
                  break;

             case OPTION_SGA:
             case OPTION_ECHO:
                  if ((data->option_state[ch] & TC_WILL) != 0) {
                      if ((data->option_state[ch] & TC_DO) == 0) {
                          data->option_state[ch] |= TC_DO;
                      }
                  }
                  break;

             case OPTION_EOR:
                  sim_debug(DEBUG_DETAIL, dptr, "unit=%d eor %d\n", unit, ch);
                  if ((data->option_state[ch] & TC_DO) == 0) {
                      data->option_state[ch] |= TC_DO;
                  }
                  break;

             case OPTION_BINARY:
                  sim_debug(DEBUG_DETAIL, dptr, "unit=%d do %d\n", unit, ch);
                  if ((data->option_state[ch] & TC_DO) == 0) {
                      data->option_state[ch] |= TC_DO;
                      scom_sendoption(uptr, unit, DO, ch);
                  }
                  break;

             default:
                 sim_debug(DEBUG_DETAIL, dptr, "unit=%d do %d\n", unit, ch);
                 if ((data->option_state[ch] & TC_WONT) == 0) {
                     data->option_state[ch] |= TC_WONT;
                     scom_sendoption(uptr, unit, WONT, ch);
                 }
                 break;
             }
             data->state = TS_DATA;
             break;

        case TS_DONT:
             sim_debug(DEBUG_DETAIL, dptr, "unit=%d dont %d\n", unit, ch);
             if ((data->option_state[ch] & TC_WILL) != 0) {
                /* send IAC WONT option */
                data->option_state[ch] &= ~TC_WILL;
                scom_sendoption(uptr, unit, WILL, ch);
             }
             data->state = TS_DATA;
             break;
        }
    }
    return SCPE_OK;
}

void
scom_sendoption(UNIT *uptr, int unit, uint8 state, uint8 opt)
{
    struct _line        *data = (struct _line *)(uptr->up7);

    sim_debug(DEBUG_DETAIL, &scom_dev, "send %d %d opt=%d\n", unit, state, opt);
    tmxr_putc_ln( &scom_ldsc[unit], IAC);
    tmxr_putc_ln( &scom_ldsc[unit], state);
    tmxr_putc_ln( &scom_ldsc[unit], opt);
    tmxr_send_buffered_data(&scom_ldsc[unit]);
    switch(state) {
    case WILL:
         data->option_state[opt] |= TC_WILL;
         break;
    case WONT:
         data->option_state[opt] |= TC_WONT;
         break;
    case DO:
         data->option_state[opt] |= TC_DO;
         break;
    case DONT:
         data->option_state[opt] |= TC_DONT;
    }
}

t_stat
scom_reset(DEVICE * dptr)
{
    int       i;
    sim_activate(&scom_unit[0], tmxr_poll);
    (void)tmxr_set_notelnet (&scom_desc);
    for (i = 0; i < NUM_UNITS_SCOM; i++)
       scoml_unit[i].up7 = &line_data[i];
    return SCPE_OK;
}


t_stat
scom_attach(UNIT * uptr, CONST char *cptr)
{
    t_stat        r;
    int           i;

    if ((r = tmxr_attach(&scom_desc, uptr, cptr)) != SCPE_OK)
       return r;
    for (i = 0; i< scom_desc.lines; i++) {
        scoml_unit[i].CMD &= ~0xffff;
    }
    sim_activate(uptr, tmxr_poll);
    return SCPE_OK;
}

t_stat
scom_detach(UNIT * uptr)
{
    t_stat        r;
    int           i;

    for (i = 0; i< scom_desc.lines; i++) {
        (void)tmxr_set_get_modem_bits(&scom_ldsc[i], 0, TMXR_MDM_DTR, NULL);
        (void)tmxr_reset_ln(&scom_ldsc[i]);
        scoml_unit[i].CMD &= ~0xffff;
    }
    sim_cancel(uptr);
    r = tmxr_detach(&scom_desc, uptr);
    return r;
}

t_stat scom_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag,
    const char *cptr)
{
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
return SCPE_OK;
}

const char *scom_description (DEVICE *dptr)
{
return "IBM 3271 communications controller";
}

#endif
