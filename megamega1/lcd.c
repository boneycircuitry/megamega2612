/*
 ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <chazwilmot@gmail.com> & <tristjpawlenty@gmail.com> wrote and adapted this file.  
 * As long as you retain this notice you can do whatever you want with this stuff. 
 * If we meet some day, and you think this stuff is worth it, you can buy me a beer.
 *                                          Charles Wilmot and Tristan Pawlenty
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <joerg@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.        Joerg Wunsch
 * ----------------------------------------------------------------------------
 *
 * Stdio demo, upper layer of LCD driver.
 *
 * $Id$
 */

#include "defines.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <avr/io.h>

#include <util/delay.h>

#include "hd44780.h"
#include "lcd.h"

/*
 * Setup the LCD controller.  First, call the hardware initialization
 * function, then adjust the display attributes we want.
 */
void
lcd_init(void)
{

  hd44780_init();

  /*
   * Clear the display.
   */
  hd44780_outcmd(HD44780_CLR);
  hd44780_wait_ready(true);

  /*
   * Entry mode: auto-increment address counter, no display shift in
   * effect.
   */
  hd44780_outcmd(HD44780_ENTMODE(1, 0));
  hd44780_wait_ready(false);

  /*
   * Enable display, activate non-blinking cursor.
   */
  hd44780_outcmd(HD44780_DISPCTL(1, 0, 0));
  hd44780_wait_ready(false);
}

/*
 * Send character c to the LCD display.  After a '\n' has been seen,
 * the next character will first clear the display.
 */
int
lcd_putchar(char c, FILE *unused)
{
  static bool nl_seen = 0;

  if (nl_seen >= 2 && c != '\n')
    {
      /*
       * First character after newline, clear display and home cursor.
       */
      hd44780_wait_ready(false);
      hd44780_outcmd(HD44780_CLR);
      hd44780_wait_ready(false);
      hd44780_outcmd(HD44780_HOME);
      hd44780_wait_ready(true);
      hd44780_outcmd(HD44780_DDADDR(0));

      nl_seen = 0;
    }
  if (c == '\n')
    {
      ++nl_seen;
      if (nl_seen == 1){
		    hd44780_wait_ready(true);
		    hd44780_outcmd(HD44780_DDADDR(0x40));
		  }
    }
  else
    {
      hd44780_wait_ready(false);
      hd44780_outdata(c);
    }

  return 0;
}

/*
Returns the cursor to the first row first cell of the LCD
*/
void home(void){
  hd44780_wait_ready(true);
	hd44780_outcmd(HD44780_DDADDR(0x00));
}

/*
Clears the screen of the LCD and returns home
*/
void clear(void){
	hd44780_wait_ready(false);
	hd44780_outcmd(HD44780_CLR);
	hd44780_wait_ready(true);
	hd44780_outcmd(HD44780_DDADDR(0));
}

/*
Puts the cursor at the start of the second row
*/
void row2(void){
	hd44780_wait_ready(true);
	hd44780_outcmd(HD44780_DDADDR(0x40));
}
