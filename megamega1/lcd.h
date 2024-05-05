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

/*
 * Initialize LCD controller.  Performs a software reset.
 */
void	lcd_init(void);

/*
 * Send one character to the LCD.
 */
int	lcd_putchar(char c, FILE *stream);

/*
Returns the cursor to the start of the LCD 
Column 0 Row 0
*/
void home(void);

/*
Clears the screen of the LCD and returns the cursor home
*/
void clear(void);

/*
Puts the cursor on the second row 
*/
void row2(void);
