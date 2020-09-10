// license:BSD-3-Clause
// copyright-holders:Phil Stroffolino, Manuel Abadia
/***************************************************************************

"Combat School" (also known as "Boot Camp") - (Konami GX611)

TODO:
- Ugly text flickering in various places, namely the text when you finish level 1.
  This is due of completely busted sprite limit hook-up. (check k007121.cpp and MT #00185)
- understand how the trackball really works for clone sets.
- it seems that to get correct target colors in firing range III we have to
  use the WRONG lookup table (the one for tiles instead of the one for
  sprites).
- in combatscb, wrong sprite/char priority (see cpu head at beginning of arm
  wrestling, and heads in intermission after firing range III)
- improve sound hook up in bootleg.
- YM2203 pitch is wrong. Fixing it screws up the tempo.

  Update: 3MHz(24MHz/8) is the more appropriate clock speed for the 2203.
  It gives the correct pitch(ear subjective) compared to the official
  soundtrack albeit the music plays slow by about 10%.

  Execution timing of the Z80 is important because it maintains music tempo
  by polling the 2203's second timer. Even when working alone with no
  context-switch the chip shouldn't be running at 1.5MHz otherwise it won't
  keep the right pace. Similar Konami games from the same period(mainevt,
  battlnts, flkatck...etc.) all have a 3.579545MHz Z80 for sound.

  In spite of adjusting clock speed polling is deemed inaccurate when
  interleaving is taken into account. A high resolution timer around the
  poll loop is probably the best bet. The driver sets its timer manually
  because strange enough, interleaving doesn't occur immediately when
  cpuexec_boost_interleave() is called. Speculations are TIME_NOWs could have
  been used as the timer durations to force instant triggering.


Credits:

    Hardware Info:
        Jose Tejada Gomez
        Manuel Abadia
        Cesareo Gutierrez

    MAME Driver:
        Phil Stroffolino
        Manuel Abadia

Memory Maps (preliminary):

***************************
* Combat School (bootleg) *
***************************

MAIN CPU:
---------
00c0-00c3   Objects control
0500        bankswitch control
0600-06ff   palette
0800-1fff   RAM
2000-2fff   Video RAM (banked)
3000-3fff   Object RAM (banked)
4000-7fff   Banked Area + IO + Video Registers
8000-ffff   ROM

SOUND CPU:
----------
0000-8000   ROM
8000-87ef   RAM
87f0-87ff   ???
9000-9001   YM2203
9008        ???
9800        OKIM5205?
a000        soundlatch?
a800        OKIM5205?
fffc-ffff   ???


        Notes about the sound systsem of the bootleg:
        ---------------------------------------------
        The positions 0x87f0-0x87ff are very important, it
        does work similar to a semaphore (same as a lot of
        vblank bits). For example in the init code, it writes
        zero to 0x87fa, then it waits to it 'll be different
        to zero, but it isn't written by this cpu. (shareram?)
        I have tried put here a K007232 chip, but it didn't
        work.

        Sound chips: OKI M5205 & YM2203

        We are using the other sound hardware for now.

****************************
* Combat School (Original) *
****************************

0000-005f   Video Registers (banked)
0400-0407   input ports
0408        coin counters
0410        bankswitch control
0600-06ff   palette
0800-1fff   RAM
2000-2fff   Video RAM (banked)
3000-3fff   Object RAM (banked)
4000-7fff   Banked Area + IO + Video Registers
8000-ffff   ROM

SOUND CPU:
----------
0000-8000   ROM
8000-87ff   RAM
9000        uPD7759
b000        uPD7759
c000        uPD7759
d000        soundlatch read
e000-e001   YM2203


2008-08:
Dip location and recommended settings verified with the US manual

***************************************************************************/

#include "emu.h"
#include "includes/combatsc.h"

#include "cpu/m6809/hd6309.h"
#include "cpu/z80/z80.h"
#include "machine/watchdog.h"
#include "sound/ym2203.h"
#include "speaker.h"


/*************************************
 *
 *  Memory handlers
 *
 *************************************/

void combatsc_state::combatsc_vreg_w(uint8_t data)
{
	if (data != m_vreg)
	{
		m_textlayer->mark_all_dirty();
		if ((data & 0x0f) != (m_vreg & 0x0f))
			m_bg_tilemap[0]->mark_all_dirty();
		if ((data >> 4) != (m_vreg >> 4))
			m_bg_tilemap[1]->mark_all_dirty();
		m_vreg = data;
	}
}

uint8_t combatsc_state::combatscb_io_r(offs_t offset)
{
	static const char *const portnames[] = { "IN0", "IN1", "DSW1", "DSW2" };

	return ioport(portnames[offset])->read();
}

void combatsc_state::combatscb_priority_w(uint8_t data)
{
	if (data & 0x40)
	{
		m_video_circuit = 1;
		m_videoram = m_page[1];
		m_scrollram = m_scrollram1;
	}
	else
	{
		m_video_circuit = 0;
		m_videoram = m_page[0];
		m_scrollram = m_scrollram0;
	}

	m_priority = data & 0x20;
}

void combatsc_state::combatsc_bankselect_w(uint8_t data)
{
	m_priority = data & 0x20;

	if (data & 0x40)
	{
		m_video_circuit = 1;
		m_videoram = m_page[1];
		m_scrollram = m_scrollram1;
	}
	else
	{
		m_video_circuit = 0;
		m_videoram = m_page[0];
		m_scrollram = m_scrollram0;
	}

	if (data & 0x10)
		membank("bank1")->set_entry((data & 0x0e) >> 1);
	else
		membank("bank1")->set_entry(8 + (data & 1));
}

void combatsc_state::combatscb_io_w(offs_t offset, uint8_t data)
{
	switch (offset)
	{
		case 0x400: combatscb_priority_w(data); break;
		case 0x800: m_soundlatch->write(data); break;
		case 0xc00: combatsc_vreg_w(data); break;
		default: m_io_ram[offset] = data; break;
	}
}

void combatsc_state::combatscb_bankselect_w(address_space &space, uint8_t data)
{
	if (data & 0x40)
	{
		m_video_circuit = 1;
		m_videoram = m_page[1];
	}
	else
	{
		m_video_circuit = 0;
		m_videoram = m_page[0];
	}

	data = data & 0x1f;

	if (data != m_bank_select)
	{
		m_bank_select = data;

		if (data & 0x10)
			membank("bank1")->set_entry((data & 0x0e) >> 1);
		else
			membank("bank1")->set_entry(8 + (data & 1));

		if (data == 0x1f)
		{
			membank("bank1")->set_entry(8 + (data & 1));
			space.install_write_handler(0x4000, 0x7fff, write8sm_delegate(*this, FUNC(combatsc_state::combatscb_io_w)));
			space.install_read_handler(0x4400, 0x4403, read8sm_delegate(*this, FUNC(combatsc_state::combatscb_io_r))); // IO RAM & Video Registers
		}
		else
		{
			space.install_read_bank(0x4000, 0x7fff, "bank1");   /* banked ROM */
			space.unmap_write(0x4000, 0x7fff);  /* banked ROM */
		}
	}
}

/****************************************************************************/

void combatsc_state::combatsc_coin_counter_w(uint8_t data)
{
	/* b7-b3: unused? */
	/* b1: coin counter 2 */
	/* b0: coin counter 1 */

	machine().bookkeeping().coin_counter_w(0, data & 0x01);
	machine().bookkeeping().coin_counter_w(1, data & 0x02);
}

uint8_t combatsc_state::trackball_r(offs_t offset)
{
	if (offset == 0)
	{
		int i, dir[4];

		for (i = 0; i < 4; i++)
		{
			uint8_t curr;

			curr = m_track_ports[i].read_safe(0xff);

			dir[i] = curr - m_pos[i];
			m_sign[i] = dir[i] & 0x80;
			m_pos[i] = curr;
		}

		/* fix sign for orthogonal movements */
		if (dir[0] || dir[1])
		{
			if (!dir[0]) m_sign[0] = m_sign[1] ^ 0x80;
			if (!dir[1]) m_sign[1] = m_sign[0];
		}
		if (dir[2] || dir[3])
		{
			if (!dir[2]) m_sign[2] = m_sign[3] ^ 0x80;
			if (!dir[3]) m_sign[3] = m_sign[2];
		}
	}

	return m_sign[offset] | (m_pos[offset] & 0x7f);
}


/* the protection is a simple multiply */
void combatsc_state::protection_w(offs_t offset, uint8_t data)
{
	m_prot[offset] = data;
}
uint8_t combatsc_state::protection_r(offs_t offset)
{
	return ((m_prot[0] * m_prot[1]) >> (offset * 8)) & 0xff;
}
void combatsc_state::protection_clock_w(uint8_t data)
{
	/* 0x3f is written here every time before accessing the other registers */
}


/****************************************************************************/

void combatsc_state::combatsc_sh_irqtrigger_w(uint8_t data)
{
	m_audiocpu->set_input_line_and_vector(0, HOLD_LINE, 0xff); // Z80
}

uint8_t combatsc_state::combatsc_busy_r()
{
	return m_upd7759->busy_r() ? 1 : 0;
}

void combatsc_state::combatsc_play_w(uint8_t data)
{
	m_upd7759->start_w(data & 2);
}

void combatsc_state::combatsc_voice_reset_w(uint8_t data)
{
	m_upd7759->reset_w(data & 1);
}

void combatsc_state::combatsc_portA_w(uint8_t data)
{
	/* unknown. always write 0 */
}

// causes scores to disappear during fire ranges, either sprite busy flag or screen frame number related
uint8_t combatsc_state::unk_r()
{
	return 0; //m_screen->frame_number() & 1;
}

/*************************************
 *
 *  Address maps
 *
 *************************************/

void combatsc_state::combatsc_map(address_map &map)
{
	map(0x0000, 0x0007).w(FUNC(combatsc_state::combatsc_pf_control_w));
	map(0x001f, 0x001f).r(FUNC(combatsc_state::unk_r));
	map(0x0020, 0x005f).rw(FUNC(combatsc_state::combatsc_scrollram_r), FUNC(combatsc_state::combatsc_scrollram_w));
//  map(0x0060, 0x00ff).writeonly();                 /* RAM */

	map(0x0200, 0x0201).rw(FUNC(combatsc_state::protection_r), FUNC(combatsc_state::protection_w));
	map(0x0206, 0x0206).w(FUNC(combatsc_state::protection_clock_w));

	map(0x0400, 0x0400).portr("IN0");
	map(0x0401, 0x0401).portr("DSW3");           /* DSW #3 */
	map(0x0402, 0x0402).portr("DSW1");           /* DSW #1 */
	map(0x0403, 0x0403).portr("DSW2");           /* DSW #2 */
	map(0x0404, 0x0407).r(FUNC(combatsc_state::trackball_r));           /* 1P & 2P controls / trackball */
	map(0x0408, 0x0408).w(FUNC(combatsc_state::combatsc_coin_counter_w));  /* coin counters */
	map(0x040c, 0x040c).w(FUNC(combatsc_state::combatsc_vreg_w));
	map(0x0410, 0x0410).nopr().w(FUNC(combatsc_state::combatsc_bankselect_w)); // read is clr a (discarded)
	map(0x0414, 0x0414).w(m_soundlatch, FUNC(generic_latch_8_device::write));
	map(0x0418, 0x0418).w(FUNC(combatsc_state::combatsc_sh_irqtrigger_w));
	map(0x041c, 0x041c).w("watchdog", FUNC(watchdog_timer_device::reset_w)); /* watchdog reset? */

	map(0x0600, 0x06ff).ram().w(m_palette, FUNC(palette_device::write_indirect)).share("palette");
	map(0x0800, 0x1fff).ram();                             /* RAM */
	map(0x2000, 0x3fff).rw(FUNC(combatsc_state::combatsc_video_r), FUNC(combatsc_state::combatsc_video_w));
	map(0x4000, 0x7fff).bankr("bank1");                        /* banked ROM area */
	map(0x8000, 0xffff).rom();                             /* ROM */
}

void combatsc_state::combatscb_map(address_map &map)
{
	map(0x0000, 0x04ff).ram();
	map(0x0500, 0x0500).w(FUNC(combatsc_state::combatscb_bankselect_w));
	map(0x0600, 0x06ff).ram().w(m_palette, FUNC(palette_device::write_indirect)).share("palette");
	map(0x0800, 0x1fff).ram();
	map(0x2000, 0x3fff).rw(FUNC(combatsc_state::combatsc_video_r), FUNC(combatsc_state::combatsc_video_w));
	map(0x4000, 0x7fff).bankr("bank1");                        /* banked ROM/RAM area */
	map(0x8000, 0xffff).rom();                             /* ROM */
}

void combatsc_state::combatsc_sound_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();                                             /* ROM */
	map(0x8000, 0x87ff).ram();                                             /* RAM */

	map(0x9000, 0x9000).w(FUNC(combatsc_state::combatsc_play_w));                  /* upd7759 play voice */
	map(0xa000, 0xa000).w(m_upd7759, FUNC(upd7759_device::port_w));                  /* upd7759 voice select */
	map(0xb000, 0xb000).r(FUNC(combatsc_state::combatsc_busy_r));                   /* upd7759 busy? */
	map(0xc000, 0xc000).w(FUNC(combatsc_state::combatsc_voice_reset_w));           /* upd7759 reset? */

	map(0xd000, 0xd000).r(m_soundlatch, FUNC(generic_latch_8_device::read)); /* soundlatch read? */
	map(0xe000, 0xe001).rw("ymsnd", FUNC(ym2203_device::read), FUNC(ym2203_device::write));   /* YM 2203 intercepted */
}

void combatsc_state::combatscb_msm_w(uint8_t data)
{
	membank("bl_abank")->set_entry(BIT(data, 7));

	m_msm->reset_w(BIT(data, 4));
	m_msm->data_w(data & 0x0f);
}

void combatsc_state::combatscb_sound_irq_ack(uint8_t data)
{
	m_audiocpu->set_input_line(0, CLEAR_LINE);
}

void combatsc_state::combatscb_sound_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();                                     /* ROM */
	map(0x8000, 0x87ff).ram();                                     /* RAM */
	map(0x9000, 0x9001).rw("ymsnd", FUNC(ym2203_device::read), FUNC(ym2203_device::write));   /* YM 2203 */
	map(0x9008, 0x9009).r("ymsnd", FUNC(ym2203_device::read));               /* ??? */
	map(0x9800, 0x9800).w(FUNC(combatsc_state::combatscb_msm_w));
	map(0xa000, 0xa000).r(m_soundlatch, FUNC(generic_latch_8_device::read)); /* soundlatch read? */
	map(0xa800, 0xa800).w(FUNC(combatsc_state::combatscb_sound_irq_ack));
	map(0xc000, 0xffff).bankr("bl_abank");
}

/*************************************
 *
 *  Input ports
 *
 *************************************/

static INPUT_PORTS_START( common_inputs )
	PORT_START("IN0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_COIN3 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START("DSW3")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Flip_Screen ) ) PORT_DIPLOCATION("SW3:1")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On )  )
	PORT_DIPUNKNOWN_DIPLOC( 0x20, 0x00, "SW3:2" )   /* Not Used according to the manual */
	PORT_SERVICE_DIPLOC( 0x40, IP_ACTIVE_LOW, "SW3:3" )
	PORT_DIPUNKNOWN_DIPLOC( 0x80, 0x00, "SW3:4" )   /* Not Used according to the manual */
INPUT_PORTS_END

static INPUT_PORTS_START( dips )
	PORT_START("DSW1")
	PORT_DIPNAME( 0x0f, 0x0f, DEF_STR( Coin_A ) ) PORT_DIPLOCATION("SW1:1,2,3,4")
	PORT_DIPSETTING(    0x02, DEF_STR( 4C_1C ) )
	PORT_DIPSETTING(    0x05, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x08, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x04, DEF_STR( 3C_2C ) )
	PORT_DIPSETTING(    0x01, DEF_STR( 4C_3C ) )
	PORT_DIPSETTING(    0x0f, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x03, DEF_STR( 3C_4C ) )
	PORT_DIPSETTING(    0x07, DEF_STR( 2C_3C ) )
	PORT_DIPSETTING(    0x0e, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x06, DEF_STR( 2C_5C ) )
	PORT_DIPSETTING(    0x0d, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0x0c, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0x0b, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0x0a, DEF_STR( 1C_6C ) )
	PORT_DIPSETTING(    0x09, DEF_STR( 1C_7C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Free_Play ) )
	PORT_DIPNAME( 0xf0, 0xf0, DEF_STR( Coin_B ) ) PORT_DIPLOCATION("SW1:5,6,7,8")
	PORT_DIPSETTING(    0x20, DEF_STR( 4C_1C ) )
	PORT_DIPSETTING(    0x50, DEF_STR( 3C_1C ) )
	PORT_DIPSETTING(    0x80, DEF_STR( 2C_1C ) )
	PORT_DIPSETTING(    0x40, DEF_STR( 3C_2C ) )
	PORT_DIPSETTING(    0x10, DEF_STR( 4C_3C ) )
	PORT_DIPSETTING(    0xf0, DEF_STR( 1C_1C ) )
	PORT_DIPSETTING(    0x30, DEF_STR( 3C_4C ) )
	PORT_DIPSETTING(    0x70, DEF_STR( 2C_3C ) )
	PORT_DIPSETTING(    0xe0, DEF_STR( 1C_2C ) )
	PORT_DIPSETTING(    0x60, DEF_STR( 2C_5C ) )
	PORT_DIPSETTING(    0xd0, DEF_STR( 1C_3C ) )
	PORT_DIPSETTING(    0xc0, DEF_STR( 1C_4C ) )
	PORT_DIPSETTING(    0xb0, DEF_STR( 1C_5C ) )
	PORT_DIPSETTING(    0xa0, DEF_STR( 1C_6C ) )
	PORT_DIPSETTING(    0x90, DEF_STR( 1C_7C ) )
	PORT_DIPSETTING(    0x00, DEF_STR( None ) )
	/* None = coin slot B disabled */

	PORT_START("DSW2")
	PORT_DIPUNKNOWN_DIPLOC( 0x01, 0x01, "SW2:1" )   /* Not Used according to the manual */
	PORT_DIPUNKNOWN_DIPLOC( 0x02, 0x02, "SW2:2" )   /* Not Used according to the manual */
	PORT_DIPNAME( 0x04, 0x00, DEF_STR( Cabinet ) ) PORT_DIPLOCATION("SW2:3")
	PORT_DIPSETTING(    0x00, DEF_STR( Upright ) )
	PORT_DIPSETTING(    0x04, DEF_STR( Cocktail ) )
	PORT_DIPUNKNOWN_DIPLOC( 0x08, 0x08, "SW2:4" )   /* Not Used according to the manual */
	PORT_DIPUNKNOWN_DIPLOC( 0x10, 0x10, "SW2:5" )   /* Not Used according to the manual */
	PORT_DIPNAME( 0x60, 0x60, DEF_STR( Difficulty ) ) PORT_DIPLOCATION("SW2:6,7")
	PORT_DIPSETTING( 0x60, DEF_STR( Easy ) )
	PORT_DIPSETTING( 0x40, DEF_STR( Normal ) )
	PORT_DIPSETTING( 0x20, DEF_STR( Difficult ) )
	PORT_DIPSETTING( 0x00, DEF_STR( Very_Difficult ) )
	PORT_DIPNAME( 0x80, 0x00, DEF_STR( Demo_Sounds ) ) PORT_DIPLOCATION("SW2:8")
	PORT_DIPSETTING( 0x80, DEF_STR( Off ) )
	PORT_DIPSETTING( 0x00, DEF_STR( On ) )
INPUT_PORTS_END

static INPUT_PORTS_START( combatsc )
	PORT_INCLUDE( dips )

	PORT_INCLUDE( common_inputs )

	PORT_START("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_PLAYER(1)
INPUT_PORTS_END

static INPUT_PORTS_START( combatsct )
	PORT_INCLUDE( dips )

	PORT_INCLUDE( common_inputs )

	/* trackball 1P */
	PORT_START("TRACK0_Y")
	PORT_BIT( 0xff, 0x00, IPT_TRACKBALL_Y ) PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_REVERSE PORT_PLAYER(1)

	PORT_START("TRACK0_X")
	PORT_BIT( 0xff, 0x00, IPT_TRACKBALL_X ) PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_PLAYER(1)

	/* trackball 2P (not implemented yet) */
	PORT_START("TRACK1_Y")
	PORT_BIT( 0xff, 0x00, IPT_TRACKBALL_Y ) PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_REVERSE PORT_PLAYER(2)

	PORT_START("TRACK1_X")
	PORT_BIT( 0xff, 0x00, IPT_TRACKBALL_X ) PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_PLAYER(2)
INPUT_PORTS_END

static INPUT_PORTS_START( combatscb )
	PORT_INCLUDE( dips )

	PORT_MODIFY("DSW2")
	PORT_DIPUNKNOWN_DIPLOC( 0x04, 0x00, "SW2:3" )
	PORT_DIPNAME( 0x10, 0x00, DEF_STR( Allow_Continue ) ) PORT_DIPLOCATION("SW2:5")
	PORT_DIPSETTING(    0x10, DEF_STR( No ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Yes ) )

	PORT_START("IN0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_START2 )

	PORT_START("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_PLAYER(1)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_PLAYER(1)
INPUT_PORTS_END



/*************************************
 *
 *  Graphics definitions
 *
 *************************************/

static const gfx_layout gfxlayout =
{
	8,8,
	0x4000,
	4,
	{ 0,1,2,3 },
	{ 0, 4, 8, 12, 16, 20, 24, 28},
	{ 0*32, 1*32, 2*32, 3*32, 4*32, 5*32, 6*32, 7*32 },
	32*8
};

static const gfx_layout tile_layout =
{
	8,8,
	0x2000, /* number of tiles */
	4,      /* bitplanes */
	{ 0*0x10000*8, 1*0x10000*8, 2*0x10000*8, 3*0x10000*8 }, /* plane offsets */
	{ 0,1,2,3,4,5,6,7 },
	{ 0*8,1*8,2*8,3*8,4*8,5*8,6*8,7*8 },
	8*8
};

static const gfx_layout sprite_layout =
{
	16,16,
	0x800,  /* number of sprites */
	4,      /* bitplanes */
	{ 3*0x10000*8, 2*0x10000*8, 1*0x10000*8, 0*0x10000*8 }, /* plane offsets */
	{
		0,1,2,3,4,5,6,7,
		16*8+0,16*8+1,16*8+2,16*8+3,16*8+4,16*8+5,16*8+6,16*8+7
	},
	{
		0*8,1*8,2*8,3*8,4*8,5*8,6*8,7*8,
		8*8,9*8,10*8,11*8,12*8,13*8,14*8,15*8
	},
	8*8*4
};

static GFXDECODE_START( gfx_combatsc )
	GFXDECODE_ENTRY( "gfx1", 0x00000, gfxlayout, 0, 8*16 )
	GFXDECODE_ENTRY( "gfx2", 0x00000, gfxlayout, 0, 8*16 )
GFXDECODE_END

static GFXDECODE_START( gfx_combatscb )
	GFXDECODE_ENTRY( "gfx1", 0x00000, tile_layout,   0, 8*16 )
	GFXDECODE_ENTRY( "gfx1", 0x40000, tile_layout,   0, 8*16 )
	GFXDECODE_ENTRY( "gfx2", 0x00000, sprite_layout, 0, 8*16 )
	GFXDECODE_ENTRY( "gfx2", 0x40000, sprite_layout, 0, 8*16 )
GFXDECODE_END


/*************************************
 *
 *  Machine driver
 *
 *************************************/

MACHINE_START_MEMBER(combatsc_state,combatsc)
{
	uint8_t *MEM = memregion("maincpu")->base() + 0x38000;

	m_io_ram  = MEM + 0x0000;
	m_page[0] = MEM + 0x4000;
	m_page[1] = MEM + 0x6000;

	m_interleave_timer = machine().scheduler().timer_alloc(timer_expired_delegate());

	membank("bank1")->configure_entries(0, 10, memregion("maincpu")->base() + 0x10000, 0x4000);

	save_item(NAME(m_priority));
	save_item(NAME(m_vreg));
	save_item(NAME(m_bank_select));
	save_item(NAME(m_video_circuit));
	save_item(NAME(m_boost));
	save_item(NAME(m_prot));
	save_item(NAME(m_pos));
	save_item(NAME(m_sign));
	save_pointer(NAME(m_page[0]),0x2000);
	save_pointer(NAME(m_page[1]),0x2000);
	save_pointer(NAME(m_scrollram0), 0x40);
	save_pointer(NAME(m_scrollram1), 0x40);
}

MACHINE_START_MEMBER(combatsc_state,combatscb)
{
	MACHINE_START_CALL_MEMBER( combatsc );
	membank("bl_abank")->configure_entries(0, 2, memregion("audiocpu")->base() + 0x8000, 0x4000);
}

void combatsc_state::machine_reset()
{
	memset(m_io_ram,  0x00, 0x4000);
	memset(m_page[0], 0x00, 0x2000);
	memset(m_page[1], 0x00, 0x2000);

	m_vreg = -1;
	m_boost = 1;
	m_bank_select = -1;
	m_prot[0] = 0;
	m_prot[1] = 0;

	for (int i = 0; i < 4; i++)
	{
		m_pos[i] = 0;
		m_sign[i] = 0;
	}

	combatsc_bankselect_w(0);
}

/* combat school (original) */
void combatsc_state::combatsc(machine_config &config)
{
	/* basic machine hardware */
	HD6309(config, m_maincpu, 3000000*4);  /* 3 MHz? */
	m_maincpu->set_addrmap(AS_PROGRAM, &combatsc_state::combatsc_map);
	m_maincpu->set_vblank_int("screen", FUNC(combatsc_state::irq0_line_hold));

	Z80(config, m_audiocpu, 3579545);   /* 3.579545 MHz */
	m_audiocpu->set_addrmap(AS_PROGRAM, &combatsc_state::combatsc_sound_map);

	config.set_maximum_quantum(attotime::from_hz(1200));

	MCFG_MACHINE_START_OVERRIDE(combatsc_state,combatsc)

	WATCHDOG_TIMER(config, "watchdog");

	/* video hardware */
	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
//  m_screen->set_refresh_hz(60);
//  m_screen->set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
//  m_screen->set_size(32*8, 32*8);
//  m_screen->set_visarea(0*8, 32*8-1, 2*8, 30*8-1);
	m_screen->set_raw(XTAL(24'000'000)/3, 528, 0, 256, 256, 16, 240); // not accurate, assuming same to other Konami games (59.17)
	m_screen->set_screen_update(FUNC(combatsc_state::screen_update_combatsc));
	m_screen->set_palette(m_palette);

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_combatsc);
	PALETTE(config, m_palette, FUNC(combatsc_state::combatsc_palette));
	m_palette->set_format(palette_device::xBGR_555, 8 * 16 * 16, 128);
	m_palette->set_endianness(ENDIANNESS_LITTLE);

	MCFG_VIDEO_START_OVERRIDE(combatsc_state,combatsc)

	K007121(config, m_k007121_1, 0);
	m_k007121_1->set_palette_tag(m_palette);
	K007121(config, m_k007121_2, 0);
	m_k007121_2->set_palette_tag(m_palette);

	/* sound hardware */
	SPEAKER(config, "mono").front_center();

	GENERIC_LATCH_8(config, m_soundlatch);

	ym2203_device &ymsnd(YM2203(config, "ymsnd", 3000000));
	ymsnd.port_a_write_callback().set(FUNC(combatsc_state::combatsc_portA_w));
	ymsnd.add_route(ALL_OUTPUTS, "mono", 0.20);

	UPD7759(config, m_upd7759).add_route(ALL_OUTPUTS, "mono", 0.70);
}


/* combat school (bootleg on different hardware) */
void combatsc_state::combatscb(machine_config &config)
{
	/* basic machine hardware */
	HD6309(config, m_maincpu, 3000000*4);  /* 3 MHz? */
	m_maincpu->set_addrmap(AS_PROGRAM, &combatsc_state::combatscb_map);
	m_maincpu->set_vblank_int("screen", FUNC(combatsc_state::irq0_line_hold));

	Z80(config, m_audiocpu, 3579545);   /* 3.579545 MHz */
	m_audiocpu->set_addrmap(AS_PROGRAM, &combatsc_state::combatscb_sound_map);

	config.set_maximum_quantum(attotime::from_hz(1200));

	MCFG_MACHINE_START_OVERRIDE(combatsc_state,combatscb)

	/* video hardware */
	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
	m_screen->set_refresh_hz(60);
	m_screen->set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
	m_screen->set_size(32*8, 32*8);
	m_screen->set_visarea(0*8, 32*8-1, 2*8, 30*8-1);
	m_screen->set_screen_update(FUNC(combatsc_state::screen_update_combatscb));
	m_screen->set_palette(m_palette);

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_combatscb);
	PALETTE(config, m_palette, FUNC(combatsc_state::combatscb_palette));
	m_palette->set_format(palette_device::xBGR_555, 8 * 16 * 16, 128);
	m_palette->set_endianness(ENDIANNESS_LITTLE);

	MCFG_VIDEO_START_OVERRIDE(combatsc_state,combatscb)

	SPEAKER(config, "mono").front_center();

	GENERIC_LATCH_8(config, m_soundlatch);
	m_soundlatch->data_pending_callback().set_inputline(m_audiocpu, INPUT_LINE_NMI);

	YM2203(config, "ymsnd", 3000000).add_route(ALL_OUTPUTS, "mono", 0.20);

	MSM5205(config, m_msm, 384000);
	m_msm->vck_callback().set_inputline("audiocpu", 0, ASSERT_LINE);
	m_msm->set_prescaler_selector(msm5205_device::S96_4B);
	m_msm->add_route(ALL_OUTPUTS, "mono", 0.30);
}



/*************************************
 *
 *  ROM definition(s)
 *
 *************************************/

ROM_START( combatsc )
	ROM_REGION( 0x40000, "maincpu", 0 ) /* 6309 code */
	ROM_LOAD( "611g01.rom", 0x30000, 0x08000, CRC(857ffffe) SHA1(de7566d58314df4b7fdc07eb31a3f9bdd12d1a73) )
	ROM_CONTINUE(           0x08000, 0x08000 )
	ROM_LOAD( "611g02.rom", 0x10000, 0x20000, CRC(9ba05327) SHA1(ea03845fb49d18ac4fca97cfffce81db66b9967b) )
	/* extra 0x8000 for banked RAM */

	ROM_REGION( 0x10000 , "audiocpu", 0 ) /* sound CPU */
	ROM_LOAD( "611g03.rom", 0x00000, 0x08000, CRC(2a544db5) SHA1(94a97c3c54bf13ccc665aa5057ac6b1d700fae2d) )

	ROM_REGION( 0x80000, "gfx1", 0 )
	ROM_LOAD16_BYTE( "611g07.rom",    0x00000, 0x40000, CRC(73b38720) SHA1(e109eb78aea464127d813284ca040e8d719599e3) )
	ROM_LOAD16_BYTE( "611g08.rom",    0x00001, 0x40000, CRC(46e7d28c) SHA1(1ece7fac954204ac35d00f3d573964fcf82dcf77) )

	ROM_REGION( 0x80000, "gfx2", 0 )
	ROM_LOAD16_BYTE( "611g11.rom",    0x00000, 0x40000, CRC(69687538) SHA1(4349a1c052a759acdf7259f8bf8c5c9489b788f2) )
	ROM_LOAD16_BYTE( "611g12.rom",    0x00001, 0x40000, CRC(9c6bf898) SHA1(eafc227b4e7df0c652ec7d78784c039c35965fdc) )

	ROM_REGION( 0x0400, "proms", 0 )
	ROM_LOAD( "611g06.h14",  0x0000, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g05.h15",  0x0100, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */
	ROM_LOAD( "611g10.h6",   0x0200, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g09.h7",   0x0300, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */

	ROM_REGION( 0x20000, "upd", 0 ) /* uPD7759 data */
	ROM_LOAD( "611g04.rom",  0x00000, 0x20000, CRC(2987e158) SHA1(87c5129161d3be29a339083349807e60b625c3f7) )

	ROM_REGION( 0x0600, "plds", 0 )
	ROM_LOAD( "ampal16l8.e7", 0x0000, 0x0104, CRC(300a9936) SHA1(a4a87e93f41392fc7d7d8601d7187d87b9f9ab01) )
	ROM_LOAD( "pal16r6.16d",  0x0200, 0x0104, NO_DUMP ) /* PAL is read protected */
	ROM_LOAD( "pal20l8.8h",   0x0400, 0x0144, NO_DUMP ) /* PAL is read protected */
ROM_END

ROM_START( combatsct )
	ROM_REGION( 0x40000, "maincpu", 0 ) /* 6309 code */
	ROM_LOAD( "g01.rom",     0x30000, 0x08000, CRC(489c132f) SHA1(c717195f89add4be4a21ecc1ddd58361b0ab4a74) )
	ROM_CONTINUE(            0x08000, 0x08000 )
	ROM_LOAD( "611g02.rom",  0x10000, 0x20000, CRC(9ba05327) SHA1(ea03845fb49d18ac4fca97cfffce81db66b9967b) )
	/* extra 0x8000 for banked RAM */

	ROM_REGION( 0x10000 , "audiocpu", 0 ) /* sound CPU */
	ROM_LOAD( "611g03.rom", 0x00000, 0x08000, CRC(2a544db5) SHA1(94a97c3c54bf13ccc665aa5057ac6b1d700fae2d) )

	ROM_REGION( 0x80000, "gfx1", 0 )
	ROM_LOAD16_BYTE( "611g07.rom",    0x00000, 0x40000, CRC(73b38720) SHA1(e109eb78aea464127d813284ca040e8d719599e3) )
	ROM_LOAD16_BYTE( "611g08.rom",    0x00001, 0x40000, CRC(46e7d28c) SHA1(1ece7fac954204ac35d00f3d573964fcf82dcf77) )

	ROM_REGION( 0x80000, "gfx2", 0 )
	ROM_LOAD16_BYTE( "611g11.rom",    0x00000, 0x40000, CRC(69687538) SHA1(4349a1c052a759acdf7259f8bf8c5c9489b788f2) )
	ROM_LOAD16_BYTE( "611g12.rom",    0x00001, 0x40000, CRC(9c6bf898) SHA1(eafc227b4e7df0c652ec7d78784c039c35965fdc) )

	ROM_REGION( 0x0400, "proms", 0 )
	ROM_LOAD( "611g06.h14",  0x0000, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g05.h15",  0x0100, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */
	ROM_LOAD( "611g10.h6",   0x0200, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g09.h7",   0x0300, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */

	ROM_REGION( 0x20000, "upd", 0 ) /* uPD7759 data */
	ROM_LOAD( "611g04.rom",  0x00000, 0x20000, CRC(2987e158) SHA1(87c5129161d3be29a339083349807e60b625c3f7) )
ROM_END

ROM_START( combatscj )
	ROM_REGION( 0x40000, "maincpu", 0 ) /* 6309 code */
	ROM_LOAD( "611p01.a14",  0x30000, 0x08000, CRC(d748268e) SHA1(91588b6a0d3af47065204b980a56544a9f29b6d9) )
	ROM_CONTINUE(            0x08000, 0x08000 )
	ROM_LOAD( "611g02.rom",  0x10000, 0x20000, CRC(9ba05327) SHA1(ea03845fb49d18ac4fca97cfffce81db66b9967b) )
	/* extra 0x8000 for banked RAM */

	ROM_REGION( 0x10000 , "audiocpu", 0 ) /* sound CPU */
	ROM_LOAD( "611g03.rom", 0x00000, 0x08000, CRC(2a544db5) SHA1(94a97c3c54bf13ccc665aa5057ac6b1d700fae2d) )

	ROM_REGION( 0x80000, "gfx1", 0 )
	ROM_LOAD16_BYTE( "611g07.rom",    0x00000, 0x40000, CRC(73b38720) SHA1(e109eb78aea464127d813284ca040e8d719599e3) )
	ROM_LOAD16_BYTE( "611g08.rom",    0x00001, 0x40000, CRC(46e7d28c) SHA1(1ece7fac954204ac35d00f3d573964fcf82dcf77) )

	ROM_REGION( 0x80000, "gfx2", 0 )
	ROM_LOAD16_BYTE( "611g11.rom",    0x00000, 0x40000, CRC(69687538) SHA1(4349a1c052a759acdf7259f8bf8c5c9489b788f2) )
	ROM_LOAD16_BYTE( "611g12.rom",    0x00001, 0x40000, CRC(9c6bf898) SHA1(eafc227b4e7df0c652ec7d78784c039c35965fdc) )

	ROM_REGION( 0x0400, "proms", 0 )
	ROM_LOAD( "611g06.h14",  0x0000, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g05.h15",  0x0100, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */
	ROM_LOAD( "611g10.h6",   0x0200, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g09.h7",   0x0300, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */

	ROM_REGION( 0x20000, "upd", 0 ) /* uPD7759 data */
	ROM_LOAD( "611g04.rom",  0x00000, 0x20000, CRC(2987e158) SHA1(87c5129161d3be29a339083349807e60b625c3f7) )
ROM_END

ROM_START( bootcamp )
	ROM_REGION( 0x40000, "maincpu", 0 ) /* 6309 code */
	ROM_LOAD( "xxx-v01.12a", 0x30000, 0x08000, CRC(c10dca64) SHA1(f34de26e998b1501e430d46e96cdc58ebc68481e) )
	ROM_CONTINUE(            0x08000, 0x08000 )
	ROM_LOAD( "611g02.rom",  0x10000, 0x20000, CRC(9ba05327) SHA1(ea03845fb49d18ac4fca97cfffce81db66b9967b) )
	/* extra 0x8000 for banked RAM */

	ROM_REGION( 0x10000 , "audiocpu", 0 ) /* sound CPU */
	ROM_LOAD( "611g03.rom", 0x00000, 0x08000, CRC(2a544db5) SHA1(94a97c3c54bf13ccc665aa5057ac6b1d700fae2d) )

	ROM_REGION( 0x80000, "gfx1", 0 )
	ROM_LOAD16_BYTE( "611g07.rom",    0x00000, 0x40000, CRC(73b38720) SHA1(e109eb78aea464127d813284ca040e8d719599e3) )
	ROM_LOAD16_BYTE( "611g08.rom",    0x00001, 0x40000, CRC(46e7d28c) SHA1(1ece7fac954204ac35d00f3d573964fcf82dcf77) )

	ROM_REGION( 0x80000, "gfx2", 0 )
	ROM_LOAD16_BYTE( "611g11.rom",    0x00000, 0x40000, CRC(69687538) SHA1(4349a1c052a759acdf7259f8bf8c5c9489b788f2) )
	ROM_LOAD16_BYTE( "611g12.rom",    0x00001, 0x40000, CRC(9c6bf898) SHA1(eafc227b4e7df0c652ec7d78784c039c35965fdc) )

	ROM_REGION( 0x0400, "proms", 0 )
	ROM_LOAD( "611g06.h14",  0x0000, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g05.h15",  0x0100, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */
	ROM_LOAD( "611g10.h6",   0x0200, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g09.h7",   0x0300, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */

	ROM_REGION( 0x20000, "upd", 0 ) /* uPD7759 data */
	ROM_LOAD( "611g04.rom",  0x00000, 0x20000, CRC(2987e158) SHA1(87c5129161d3be29a339083349807e60b625c3f7) )
ROM_END

ROM_START( bootcampa )
	ROM_REGION( 0x40000, "maincpu", 0 ) /* 6309 code */
	ROM_LOAD( "611x01.a-14", 0x30000, 0x08000, CRC(98ffc6ed) SHA1(ab02532333272683d889f209d3fc01235871d909) )
	ROM_CONTINUE(            0x08000, 0x08000 )
	ROM_LOAD( "611g02.rom",  0x10000, 0x20000, CRC(9ba05327) SHA1(ea03845fb49d18ac4fca97cfffce81db66b9967b) )
	/* extra 0x8000 for banked RAM */

	ROM_REGION( 0x10000 , "audiocpu", 0 ) /* sound CPU */
	ROM_LOAD( "611g03.rom", 0x00000, 0x08000, CRC(2a544db5) SHA1(94a97c3c54bf13ccc665aa5057ac6b1d700fae2d) )

	ROM_REGION( 0x80000, "gfx1", 0 )
	ROM_LOAD16_BYTE( "611g07.rom",    0x00000, 0x40000, CRC(73b38720) SHA1(e109eb78aea464127d813284ca040e8d719599e3) )
	ROM_LOAD16_BYTE( "611g08.rom",    0x00001, 0x40000, CRC(46e7d28c) SHA1(1ece7fac954204ac35d00f3d573964fcf82dcf77) )

	ROM_REGION( 0x80000, "gfx2", 0 )
	ROM_LOAD16_BYTE( "611g11.rom",    0x00000, 0x40000, CRC(69687538) SHA1(4349a1c052a759acdf7259f8bf8c5c9489b788f2) )
	ROM_LOAD16_BYTE( "611g12.rom",    0x00001, 0x40000, CRC(9c6bf898) SHA1(eafc227b4e7df0c652ec7d78784c039c35965fdc) )

	ROM_REGION( 0x0400, "proms", 0 )
	ROM_LOAD( "611g06.h14",  0x0000, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g05.h15",  0x0100, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */
	ROM_LOAD( "611g10.h6",   0x0200, 0x0100, CRC(f916129a) SHA1(d5e4a8a3baab8fcdac86ef5182858cede1abf040) ) /* sprites lookup table */
	ROM_LOAD( "611g09.h7",   0x0300, 0x0100, CRC(207a7b07) SHA1(f4e638e7f182e5228a062b243406d0ceaaa5bfdc) ) /* chars lookup table */

	ROM_REGION( 0x20000, "upd", 0 ) /* uPD7759 data */
	ROM_LOAD( "611g04.rom",  0x00000, 0x20000, CRC(2987e158) SHA1(87c5129161d3be29a339083349807e60b625c3f7) )
ROM_END

ROM_START( combatscb )
	ROM_REGION( 0x40000, "maincpu", 0 ) /* 6809 code */
	ROM_LOAD( "combat.002",  0x30000, 0x08000, CRC(0996755d) SHA1(bb6bbbf7ab3b5fab5e1c6cebc7b3f0d720493c3b) )
	ROM_CONTINUE(            0x08000, 0x08000 )
	ROM_LOAD( "combat.003",  0x10000, 0x10000, CRC(229c93b2) SHA1(ac3fd3df1bb5f6a461d0d1423c50568348ef69df) )
	ROM_LOAD( "combat.004",  0x20000, 0x10000, CRC(a069cb84) SHA1(f49f70afb17df46b16f5801ef42edb0706730723) )
	/* extra 0x8000 for banked RAM */

	ROM_REGION( 0x10000 , "audiocpu", 0 ) /* sound CPU */
	ROM_LOAD( "combat.001",  0x00000, 0x10000, CRC(61456b3b) SHA1(320db628283dd1bec465e95020d1a1158e6d6ae4) )

	ROM_REGION( 0x80000, "gfx1", ROMREGION_INVERT )
	ROM_LOAD( "combat.006",  0x00000, 0x10000, CRC(8dc29a1f) SHA1(564dd7c6acff34db93b8e300dda563f5f38ba159) ) /* tiles, bank 0 */
	ROM_LOAD( "combat.008",  0x10000, 0x10000, CRC(61599f46) SHA1(cfd79a88bb496773daf207552c67f595ee696bc4) )
	ROM_LOAD( "combat.010",  0x20000, 0x10000, CRC(d5cda7cd) SHA1(140db6270c3f358aa27013db3bb819a48ceb5142) )
	ROM_LOAD( "combat.012",  0x30000, 0x10000, CRC(ca0a9f57) SHA1(d6b3daf7c34345bb2f64068d480bd51d7bb36e4d) )
	ROM_LOAD( "combat.005",  0x40000, 0x10000, CRC(0803a223) SHA1(67d4162385dd56d5396e181070bfa6760521eb45) ) /* tiles, bank 1 */
	ROM_LOAD( "combat.007",  0x50000, 0x10000, CRC(23caad0c) SHA1(0544cde479c6d4192da5bb4b6f0e2e75d09663c3) )
	ROM_LOAD( "combat.009",  0x60000, 0x10000, CRC(5ac80383) SHA1(1e89c371a92afc000d593daebda4156952a15244) )
	ROM_LOAD( "combat.011",  0x70000, 0x10000, CRC(cda83114) SHA1(12d2a9f694287edb3bb0ee7a8ba0e0724dad8e1f) )

	ROM_REGION( 0x80000, "gfx2", ROMREGION_INVERT )
	ROM_LOAD( "combat.013",  0x00000, 0x10000, CRC(4bed2293) SHA1(3369de47d4ba041d9f17a18dcca2af7ac9f8bc0c) ) /* sprites, bank 0 */
	ROM_LOAD( "combat.015",  0x10000, 0x10000, CRC(26c41f31) SHA1(f8eb7d0729a21a0dd92ce99c9cda0cde9526b861) )
	ROM_LOAD( "combat.017",  0x20000, 0x10000, CRC(6071e6da) SHA1(ba5f8e83b07faaffc564d3568630e17efdb5a09f) )
	ROM_LOAD( "combat.019",  0x30000, 0x10000, CRC(3b1cf1b8) SHA1(ff4de37c051bcb374c44d1b99006ff6ff5e1f927) )
	ROM_LOAD( "combat.014",  0x40000, 0x10000, CRC(82ea9555) SHA1(59bf7836938ce9e3242d1cca754de8dbe85bbfb7) ) /* sprites, bank 1 */
	ROM_LOAD( "combat.016",  0x50000, 0x10000, CRC(2e39bb70) SHA1(a6c4acd93cc803e987de6e18fbdc5ce4634b14a8) )
	ROM_LOAD( "combat.018",  0x60000, 0x10000, CRC(575db729) SHA1(6b1676da4f24fc90c77262789b6cc116184ab912) )
	ROM_LOAD( "combat.020",  0x70000, 0x10000, CRC(8d748a1a) SHA1(4386e14e19b91e053033dde2a13019bc6d8e1d5a) )

	ROM_REGION( 0x0200, "proms", 0 )
	ROM_LOAD( "prom.d10",    0x0000, 0x0100, CRC(265f4c97) SHA1(76f1b75a593d3d77ef6173a1948f842d5b27d418) ) /* sprites lookup table */
	ROM_LOAD( "prom.c11",    0x0100, 0x0100, CRC(a7a5c0b4) SHA1(48bfc3af40b869599a988ebb3ed758141bcfd4fc) ) /* priority? */
ROM_END


/*************************************
 *
 *  Driver initialization
 *
 *************************************/

void combatsc_state::init_combatsc()
{
	/* joystick instead of trackball */
	m_maincpu->space(AS_PROGRAM).install_read_port(0x0404, 0x0404, "IN1");
}


/*************************************
 *
 *  Game driver(s)
 *
 *************************************/

GAME( 1988, combatsc,  0,        combatsc,  combatsc,  combatsc_state, init_combatsc, ROT0, "Konami",  "Combat School (joystick)",        0 )
GAME( 1987, combatsct, combatsc, combatsc,  combatsct, combatsc_state, empty_init,    ROT0, "Konami",  "Combat School (trackball)",       MACHINE_NOT_WORKING )
GAME( 1987, combatscj, combatsc, combatsc,  combatsct, combatsc_state, empty_init,    ROT0, "Konami",  "Combat School (Japan trackball)", MACHINE_NOT_WORKING )
GAME( 1987, bootcamp,  combatsc, combatsc,  combatsct, combatsc_state, empty_init,    ROT0, "Konami",  "Boot Camp (set 1)",               MACHINE_NOT_WORKING )
GAME( 1987, bootcampa, combatsc, combatsc,  combatsct, combatsc_state, empty_init,    ROT0, "Konami",  "Boot Camp (set 2)",               MACHINE_NOT_WORKING )
GAME( 1988, combatscb, combatsc, combatscb, combatscb, combatsc_state, empty_init,    ROT0, "bootleg", "Combat School (bootleg)",         MACHINE_IMPERFECT_COLORS | MACHINE_IMPERFECT_SOUND )
