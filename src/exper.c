/* NetHack 3.6	exper.c	$NHDT-Date: 1562114352 2019/07/03 00:39:12 $  $NHDT-Branch: NetHack-3.6 $:$NHDT-Revision: 1.33 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2007. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#ifndef LONG_MAX
#include <limits.h>
#endif

STATIC_DCL int FDECL(enermod, (int));

long
newuexp(lev)
int lev;
{
    if (lev < 1) /* for newuexp(u.ulevel - 1) when u.ulevel is 1 */
        return 0L;
    if (lev < 10)
        return (10L * (1L << lev));
    if (lev < 17)
        return (10000L * (1L << (lev - 10)));
    return (320000L * ((long) (lev - 14)));
}

STATIC_OVL int
enermod(en)
int en;
{
    switch (Role_switch) {
    case PM_PRIEST:
    case PM_WIZARD:
        return (2 * en);
    case PM_DRUID:
    case PM_HEALER:
    case PM_INFIDEL:
    case PM_KNIGHT:
        return ((3 * en) / 2);
    case PM_BARBARIAN:
    case PM_VALKYRIE:
        return ((3 * en) / 4);
    default:
        return en;
    }
}

/* calculate spell power/energy points for new level */
int
newpw()
{
    int en = 0, enrnd, enfix;

    if (u.ulevel == 0) {
        en = urole.enadv.infix + urace.enadv.infix;
        if (urole.enadv.inrnd > 0)
            en += rnd(urole.enadv.inrnd);
        if (urace.enadv.inrnd > 0)
            en += rnd(urace.enadv.inrnd);
    } else {
        enrnd = (int) ACURR(A_WIS) / 2;
        if (u.ulevel < urole.xlev) {
            enrnd += urole.enadv.lornd + urace.enadv.lornd;
            enfix = urole.enadv.lofix + urace.enadv.lofix;
        } else {
            enrnd += urole.enadv.hirnd + urace.enadv.hirnd;
            enfix = urole.enadv.hifix + urace.enadv.hifix;
        }
        en = enermod(rn1(enrnd, enfix));
    }
    if (en <= 0)
        en = 1;
    if (u.ulevel < MAXULEV) {
        /* remember increment; future level drain could take it away again */
        u.ueninc[u.ulevel] = (xchar) en;
    } else {
        /* after level 30, throttle energy gains from extra experience;
           once max reaches 600, further increments will be just 1 more */
        char lim = 4 - u.uenmax / 200;

        lim = max(lim, 1);
        if (en > lim)
            en = lim;
    }
    return en;
}

/* return # of exp points for mtmp after nk killed */
int
experience(mtmp, nk)
register struct monst *mtmp;
register int nk;
{
    register struct permonst *ptr = mtmp->data;
    int i, tmp, tmp2;

    tmp = 1 + mtmp->m_lev * mtmp->m_lev;

    /*  For higher ac values, give extra experience */
    if ((i = find_mac(mtmp)) < 3)
        tmp += (7 - i) * ((i < 0) ? 2 : 1);

    /*  For very fast monsters, give extra experience */
    if (ptr->mmove > NORMAL_SPEED)
        tmp += (ptr->mmove > (3 * NORMAL_SPEED / 2)) ? 5 : 3;

    /*  For each "special" attack type give extra experience */
    for (i = 0; i < NATTK; i++) {
        tmp2 = ptr->mattk[i].aatyp;
        if (tmp2 > AT_BUTT) {
            if (tmp2 == AT_WEAP)
                tmp += 5;
            else if (tmp2 == AT_MAGC)
                tmp += 10;
            else
                tmp += 3;
        }
    }

    /*  For each "special" damage type give extra experience */
    for (i = 0; i < NATTK; i++) {
        tmp2 = ptr->mattk[i].adtyp;
        if (tmp2 > AD_PHYS && tmp2 < AD_BLND)
            tmp += 2 * mtmp->m_lev;
        else if ((tmp2 == AD_DRLI) || (tmp2 == AD_STON) || (tmp2 == AD_SLIM))
            tmp += 50;
        else if (tmp2 != AD_PHYS)
            tmp += mtmp->m_lev;
        /* extra heavy damage bonus */
        if ((int) (ptr->mattk[i].damd * ptr->mattk[i].damn) > 23)
            tmp += mtmp->m_lev;
        if (tmp2 == AD_WRAP && ptr->mlet == S_EEL
            && !Amphibious && !Breathless)
            tmp += 1000;
    }

    /*  For certain "extra nasty" monsters, give even more */
    if (extra_nasty(ptr))
        tmp += (7 * mtmp->m_lev);

    /*  For higher level monsters, an additional bonus is given */
    if (mtmp->m_lev > 8)
        tmp += 50;

#ifdef MAIL
    /* Mail daemons put up no fight. */
    if (mtmp->data == &mons[PM_MAIL_DAEMON])
        tmp = 1;
#endif

    if (mtmp->mrevived || mtmp->mcloned) {
        /*
         *      Reduce experience awarded for repeated killings of
         *      "the same monster".  Kill count includes all of this
         *      monster's type which have been killed--including the
         *      current monster--regardless of how they were created.
         *        1.. 20        full experience
         *       21.. 40        xp / 2
         *       41.. 80        xp / 4
         *       81..120        xp / 8
         *      121..180        xp / 16
         *      181..240        xp / 32
         *      241..255+       xp / 64
         */
        for (i = 0, tmp2 = 20; nk > tmp2 && tmp > 1; ++i) {
            tmp = (tmp + 1) / 2;
            nk -= tmp2;
            if (i & 1)
                tmp2 += 20;
        }
    }

    return (tmp);
}

void
more_experienced(exper, rexp)
register int exper, rexp;
{
    long oldexp = u.uexp,
         oldrexp = u.urexp,
         newexp = oldexp + exper,
         rexpincr = 4 * exper + rexp,
         newrexp = oldrexp + rexpincr;

    /* cap experience and score on wraparound */
    if (newexp < 0 && exper > 0)
        newexp = LONG_MAX;
    if (newrexp < 0 && rexpincr > 0)
        newrexp = LONG_MAX;

    if (newexp != oldexp) {
        u.uexp = newexp;
        if (flags.showexp)
            context.botl = TRUE;
        /* even when experience points aren't being shown, experience level
           might be highlighted with a percentage highlight rule and that
           percentage depends upon experience points */
        if (!context.botl && exp_percent_changing())
            context.botl = TRUE;
    }
    /* newrexp will always differ from oldrexp unless they're LONG_MAX */
    if (newrexp != oldrexp) {
        u.urexp = newrexp;
#ifdef SCORE_ON_BOTL
        if (flags.showscore)
            context.botl = TRUE;
#endif
    }
    if (u.urexp >= (Role_if(PM_WIZARD) ? 1000 : 2000))
        flags.beginner = 0;
}

/* e.g., hit by drain life attack */
void
losexp(drainer)
const char *drainer; /* cause of death, if drain should be fatal */
{
    register int num;

    /* override life-drain resistance when handling an explicit
       wizard mode request to reduce level; never fatal though */
    if (drainer && !strcmp(drainer, "#levelchange"))
        drainer = 0;
    else if (resists_drli(&youmonst))
        return;

    if (u.ulevel > 1) {
        pline("%s level %d.", Goodbye(), u.ulevel--);
        /* remove intrinsic abilities */
        adjabil(u.ulevel + 1, u.ulevel);
    } else {
        if (drainer) {
            killer.format = KILLED_BY;
            if (killer.name != drainer)
                Strcpy(killer.name, drainer);
            done(DIED);
        }
        /* no drainer or lifesaved */
        u.uexp = 0;
    }
    num = (int) u.uhpinc[u.ulevel];
    u.uhpmax -= num;
    if (u.uhpmax < 1)
        u.uhpmax = 1;
    u.uhp -= num;
    if (u.uhp < 1)
        u.uhp = 1;
    else if (u.uhp > u.uhpmax)
        u.uhp = u.uhpmax;

    num = (int) u.ueninc[u.ulevel];
    u.uenmax -= num;
    if (u.uenmax < 0)
        u.uenmax = 0;
    u.uen -= num;
    if (u.uen < 0)
        u.uen = 0;
    else if (u.uen > u.uenmax)
        u.uen = u.uenmax;

    if (u.uexp > 0)
        u.uexp = newuexp(u.ulevel) - 1;

    if (Upolyd) {
        num = monhp_per_lvl(&youmonst);
        u.mhmax -= num;
        u.mh -= num;
        if (u.mh <= 0)
            rehumanize();
    }

    context.botl = TRUE;
}

/*
 * Make experience gaining similar to AD&D(tm), whereby you can at most go
 * up by one level at a time, extra expr possibly helping you along.
 * After all, how much real experience does one get shooting a wand of death
 * at a dragon created with a wand of polymorph??
 */
void
newexplevel()
{
    if (u.ulevel < MAXULEV && u.uexp >= newuexp(u.ulevel))
        pluslvl(TRUE);
}

void
pluslvl(incr)
boolean incr; /* true iff via incremental experience growth */
{             /*        (false for potion of gain level)    */
    int hpinc, eninc;

    if (!incr)
        You_feel("more experienced.");

    /* increase hit points (when polymorphed, do monster form first
       in order to retain normal human/whatever increase for later) */
    if (Upolyd) {
        hpinc = monhp_per_lvl(&youmonst);
        u.mhmax += hpinc;
        u.mh += hpinc;
    }
    hpinc = newhp();
    u.uhpmax += hpinc;
    u.uhp += hpinc;

    /* increase spell power/energy points */
    eninc = newpw();
    u.uenmax += eninc;
    u.uen += eninc;

    /* increase level (unless already maxxed) */
    if (u.ulevel < MAXULEV) {
        /* increase experience points to reflect new level */
        if (incr) {
            long tmp = newuexp(u.ulevel + 1);
            if (u.uexp >= tmp)
                u.uexp = tmp - 1;
        } else {
            u.uexp = newuexp(u.ulevel);
        }
        ++u.ulevel;
        pline("Welcome %sto experience level %d.",
              (u.ulevelmax < u.ulevel) ? "" : "back ",
              u.ulevel);
        if (u.ulevelmax < u.ulevel)
            u.ulevelmax = u.ulevel;
        adjabil(u.ulevel - 1, u.ulevel); /* give new intrinsics */
    }
    context.botl = TRUE;
}

/* compute a random amount of experience points suitable for the hero's
   experience level:  base number of points needed to reach the current
   level plus a random portion of what it takes to get to the next level */
long
rndexp(gaining)
boolean gaining; /* gaining XP via potion vs setting XP for polyself */
{
    long minexp, maxexp, diff, factor, result;

    minexp = (u.ulevel == 1) ? 0L : newuexp(u.ulevel - 1);
    maxexp = newuexp(u.ulevel);
    diff = maxexp - minexp, factor = 1L;
    /* make sure that `diff' is an argument which rn2() can handle */
    while (diff >= (long) LARGEST_INT)
        diff /= 2L, factor *= 2L;
    result = minexp + factor * (long) rn2((int) diff);
    /* 3.4.1:  if already at level 30, add to current experience
       points rather than to threshold needed to reach the current
       level; otherwise blessed potions of gain level can result
       in lowering the experience points instead of raising them */
    if (u.ulevel == MAXULEV && gaining) {
        result += (u.uexp - minexp);
        /* avoid wrapping (over 400 blessed potions needed for that...) */
        if (result < u.uexp)
            result = u.uexp;
    }
    return result;
}

/*exper.c*/
