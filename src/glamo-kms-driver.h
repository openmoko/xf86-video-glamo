/*
 * KMS Support for the SMedia Glamo3362 X.org Driver
 *
 * Copyright 2009 Thomas White <taw@bitwiz.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */

extern Bool GlamoKMSPreInit(ScrnInfoPtr pScrn, int flags);
extern Bool GlamoKMSScreenInit(int scrnIndex, ScreenPtr pScreen, int argc,
                               char **argv);
extern Bool SwitchMode(int scrnIndex, DisplayModePtr mode, int flags);
extern void AdjustFrame(int scrnIndex, int x, int y, int flags);
extern Bool EnterVT(int scrnIndex, int flags);
extern void LeaveVT(int scrnIndex, int flags);
extern ModeStatus ValidMode(int scrnIndex, DisplayModePtr mode, Bool verbose,
                            int flags);
