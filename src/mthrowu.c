/* NetHack 3.6	mthrowu.c	$NHDT-Date: 1573688695 2019/11/13 23:44:55 $  $NHDT-Branch: NetHack-3.6 $:$NHDT-Revision: 1.86 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Pasi Kallinen, 2016. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "mfndpos.h" /* ALLOW_M */

STATIC_DCL int FDECL(monmulti, (struct monst *, struct obj *, struct obj *));
STATIC_DCL void FDECL(monshoot, (struct monst *, struct obj *, struct obj *));
STATIC_DCL int FDECL(drop_throw, (struct obj *, BOOLEAN_P, int, int));

#define URETREATING(x, y) \
    (distmin(u.ux, u.uy, x, y) > distmin(u.ux0, u.uy0, x, y))

#define POLE_LIM 5 /* How far monsters can use pole-weapons */

#define PET_MISSILE_RANGE2 36 /* Square of distance within which pets shoot */

/*
 * Keep consistent with breath weapons in zap.c, and AD_* in monattk.h.
 */
STATIC_OVL NEARDATA const char *breathwep[] = {
    "fragments", "fire", "frost", "sleep gas",  "a disintegration blast",
    "lightning", "poison gas", "acid", "water", "a dark energy blast",
    "a disorienting blast", "strange breath #11"
};

extern boolean notonhead; /* for long worms */
STATIC_VAR int mesg_given; /* for m_throw()/thitu() 'miss' message */

static struct obj *ammo_stack = 0;

boolean
m_has_launcher_and_ammo(mtmp)
struct monst *mtmp;
{
    struct obj *mwep = MON_WEP(mtmp);

    if (mwep && is_launcher(mwep)) {
        struct obj *otmp;

        for (otmp = mtmp->minvent; otmp; otmp = otmp->nobj)
            if (ammo_and_launcher(otmp, mwep))
                return TRUE;
    }
    return FALSE;
}

/* hero is hit by something other than a monster */
int
thitu(tlev, dam, objp, name)
int tlev, dam;
struct obj **objp;
const char *name; /* if null, then format `*objp' */
{
    struct obj *obj = objp ? *objp : 0;
    const char *onm, *knm;
    int realac;
    boolean is_acid, is_webbing, is_needle;
    int kprefix = KILLED_BY_AN, dieroll;
    char onmbuf[BUFSZ], knmbuf[BUFSZ];

    if (!name) {
        if (!obj)
            panic("thitu: name & obj both null?");
        name = strcpy(onmbuf,
                      (obj->quan > 1L) ? doname(obj) : mshot_xname(obj));
        knm = strcpy(knmbuf, killer_xname(obj));
        kprefix = KILLED_BY; /* killer_name supplies "an" if warranted */
    } else {
        knm = name;
        /* [perhaps ought to check for plural here to] */
        if (!strncmpi(name, "the ", 4) || !strncmpi(name, "an ", 3)
            || !strncmpi(name, "a ", 2))
            kprefix = KILLED_BY;
    }
    onm = (obj && obj_is_pname(obj)) ? the(name)
          : (obj && obj->quan > 1L) ? name
            : an(name);
    is_acid = (obj && obj->otyp == ACID_VENOM);
    is_webbing = (obj && obj->otyp == BALL_OF_WEBBING);
    is_needle = (obj && obj->otyp == BARBED_NEEDLE);

    realac = AC_VALUE(u.uac);
    if (realac + tlev <= (dieroll = rnd(20))) {
        ++mesg_given;
        if (Blind || !flags.verbose) {
            pline("It misses.");
        } else if (u.uac + tlev <= dieroll - 2) {
            if (onm != onmbuf)
                Strcpy(onmbuf, onm); /* [modifiable buffer for upstart()] */
            if ((thick_skinned(youmonst.data)
                 || (!Upolyd && Race_if(PM_TORTLE))
                 || Barkskin || Stoneskin) && rn2(2)) {
                Your("%s %s %s.",
                     (is_dragon(youmonst.data)
                      ? "scaly hide"
                      : (youmonst.data == &mons[PM_GIANT_TURTLE]
                         || (maybe_polyd(is_tortle(youmonst.data),
                                         Race_if(PM_TORTLE))))
                        ? "protective shell"
                        : is_bone_monster(youmonst.data)
                          ? "bony structure"
                          : (has_bark(youmonst.data) || Barkskin)
                            ? "rough bark"
                            : Stoneskin ? "stony hide" : "thick hide"),
                     (rn2(2) ? "blocks" : "deflects"), onm);
            } else if (uarms && rn2(2)) {
                boolean nameart = (uarms->oartifact && uarms->dknown);
                pline("%s%s %s %s.", nameart ? "Your " : "",
                      nameart ? xname(uarms)
                              : Ysimple_name2(uarms),
                      (rn2(2) ? "blocks" : "deflects"), onm);
                use_skill(P_SHIELD, 1);
            } else {
                pline("%s %s you.", upstart(onmbuf), vtense(onmbuf, "miss"));
            }
        } else
            You("are almost hit by %s.", onm);

        return 0;
    } else {
        if (obj
            && (obj->oartifact ||
                ((obj->oclass == WEAPON_CLASS
                  || is_weptool(obj) || is_bullet(obj))
                 && obj->oprops & ITEM_PROP_MASK)))
            (void) artifact_hit((struct monst *) 0, &youmonst, obj, &dam, 0);
        else if (Blind || !flags.verbose)
            You("are hit%s", exclam(dam));
        else
            You("are hit by %s%s", onm, exclam(dam));

        if (ammo_stack)
            ammo_stack->oprops_known |= obj->oprops_known;

        if (is_acid && Acid_resistance) {
            pline("It doesn't seem to hurt you.");
            monstseesu(M_SEEN_ACID);
        } else if (is_needle && Poison_resistance) {
            pline("It doesn't seem to affect you.");
            monstseesu(M_SEEN_POISON);
        } else if (is_webbing && (is_pool_or_lava(u.ux, u.uy)
                                  || is_puddle(u.ux, u.uy)
                                  || is_sewage(u.ux, u.uy)
                                  || IS_AIR(levl[u.ux][u.uy].typ))) {
            You("easily disentangle yourself.");
        } else if (obj && obj->oclass == POTION_CLASS) {
            /* an explosion which scatters objects might hit hero with one
               (potions deliberately thrown at hero are handled by m_throw) */
            potionhit(&youmonst, obj, POTHIT_OTHER_THROW);
            *objp = obj = 0; /* potionhit() uses up the potion */
        } else {
            if (obj && Hate_material(obj->material)) {
                /* extra damage already applied by dmgval();
                 * dmgval is not called in this function but we assume that the
                 * caller used it when constructing the dmg parameter */
                searmsg((struct monst *) 0, &youmonst, obj, FALSE);
                exercise(A_CON, FALSE);
            }
            if (is_acid)
                pline("It burns!");
            if (is_needle)
                pline("Ow, that stings!");
            losehp(dam, knm, kprefix); /* acid damage */
            exercise(A_STR, FALSE);
        }
        return 1;
    }
}

/* Be sure this corresponds with what happens to player-thrown objects in
 * dothrow.c (for consistency). --KAA
 * Returns 0 if object still exists (not destroyed).
 */
STATIC_OVL int
drop_throw(obj, ohit, x, y)
register struct obj *obj;
boolean ohit;
int x, y;
{
    int retvalu = 1;
    int create;
    struct monst *mtmp;
    struct trap *t;

    if (obj->otyp == CREAM_PIE || obj->oclass == VENOM_CLASS
        || (ohit && obj->otyp == EGG))
        create = 0;
    else if (ohit && (is_multigen(obj) || obj->otyp == ROCK))
        create = !rn2(3);
    else
        create = 1;

    if (create && !((mtmp = m_at(x, y)) != 0 && mtmp->mtrapped
                    && (t = t_at(x, y)) != 0
                    && is_pit(t->ttyp))) {
        int objgone = 0;

        if (!IS_SOFT(levl[x][y].typ) && breaktest(obj, x, y)) {
            breakmsg(obj, cansee(x, y));
            breakobj(obj, x, y, FALSE, FALSE);
            objgone = 1;
        }
        if (!objgone && down_gate(x, y) != -1)
            objgone = ship_object(obj, x, y, FALSE);
        if (!objgone) {
            if (!flooreffects(obj, x, y, "fall")) {
                int presult = ER_NOTHING;
                place_object(obj, x, y);
                if (!mtmp && x == u.ux && y == u.uy)
                    mtmp = &youmonst;
                if (mtmp && ohit)
                    presult = passive_obj(mtmp, obj, (struct attack *) 0);
                if (presult != ER_DESTROYED)
                    stackobj(obj);
                retvalu = 0;
            }
        }
    } else {
        obfree(obj, (struct obj *) 0);
    }
    thrownobj = 0;
    return retvalu;
}

/* The monster that's being shot at when one monster shoots at another */
STATIC_OVL struct monst *target = 0;
/* The monster that's doing the shooting/throwing */
STATIC_OVL struct monst *archer = 0;

/* calculate multishot volley count for mtmp throwing otmp (if not ammo) or
   shooting otmp with mwep (if otmp is ammo and mwep appropriate launcher) */
STATIC_OVL int
monmulti(mtmp, otmp, mwep)
struct monst *mtmp;
struct obj *otmp, *mwep;
{
    int skill = (int) objects[otmp->otyp].oc_skill;
    int multishot = 1;

    if (otmp->quan > 1L /* no point checking if there's only 1 */
        /* ammo requires corresponding launcher be wielded */
        && (is_ammo(otmp)
               ? matching_launcher(otmp, mwep)
               /* otherwise any stackable (non-ammo) weapon */
               : otmp->oclass == WEAPON_CLASS)
        && !mtmp->mconf) {
        /* Assumes lords are skilled, princes are expert */
        if (is_prince(mtmp->data))
            multishot += 2;
        else if (is_lord(mtmp->data))
            multishot++;
        /* fake players treated as skilled (regardless of role limits) */
        else if (is_mplayer(mtmp->data))
            multishot++;
        /* Medusa is an expert archer */
        else if (mtmp->data == &mons[PM_MEDUSA])
            multishot += 2;
        /* Mariliths have multiple pairs of limbs */
        else if (mtmp->data == &mons[PM_MARILITH])
            multishot += 2;
        /* this portion is different from hero multishot; from slash'em?
         */
        /* Elvenkind Craftsmanship makes for light, quick bows */
        if ((otmp->otyp == ELVEN_ARROW
             || otmp->otyp == DARK_ELVEN_ARROW) && !otmp->cursed)
            multishot++;
        /* for arrow, we checked bow&arrow when entering block, but for
           bow, so far we've only validated that otmp is a weapon stack;
           need to verify that it's a stack of arrows rather than darts */
        if (mwep && (mwep->otyp == ELVEN_BOW
                     || mwep->otyp == DARK_ELVEN_BOW)
            && ammo_and_launcher(otmp, mwep) && !mwep->cursed)
            multishot++;
        /* 1/3 of launcher enchantment */
        if (ammo_and_launcher(otmp, mwep) && mwep->spe > 1)
            multishot += (long) rounddiv(mwep->spe, 3);
        /* Some randomness */
        multishot = (long) rnd((int) multishot);

        /* class bonus */
        switch (monsndx(mtmp->data)) {
        case PM_CAVEMAN: /* give bonus for low-tech gear */
        case PM_CAVEWOMAN:
            if (skill == -P_SLING || skill == P_SPEAR)
                multishot++;
            break;
        case PM_MONK: /* allow higher volley count */
            if (skill == -P_SHURIKEN)
                multishot++;
            break;
        case PM_RANGER:
            if (skill != P_DAGGER)
                multishot++;
            break;
        case PM_ROGUE:
            if (skill == P_DAGGER)
                multishot++;
            break;
        case PM_NINJA:
            if (skill == -P_SHURIKEN || skill == -P_DART)
                multishot++;
            /*FALLTHRU*/
        case PM_SAMURAI:
            if (otmp->otyp == YA && mwep->otyp == YUMI)
                multishot++;
            break;
        default:
            break;
        }
        /* racial bonus */
        if ((racial_elf(mtmp) && otmp->otyp == ELVEN_ARROW
            && mwep->otyp == ELVEN_BOW)
            || (racial_drow(mtmp) && otmp->otyp == DARK_ELVEN_ARROW
                && mwep->otyp == DARK_ELVEN_BOW)
            || (racial_orc(mtmp) && otmp->otyp == ORCISH_ARROW
                && mwep->otyp == ORCISH_BOW)
            || (racial_drow(mtmp) && otmp->otyp == DARK_ELVEN_CROSSBOW_BOLT
                && mwep->otyp == DARK_ELVEN_HAND_CROSSBOW)
            || (racial_gnome(mtmp) && otmp->otyp == CROSSBOW_BOLT
                && mwep->otyp == CROSSBOW))
            multishot++;
    }

    if (otmp->quan < multishot)
        multishot = (int) otmp->quan;
    if (multishot < 1)
        multishot = 1;
    return multishot;
}

/* mtmp throws otmp, or shoots otmp with mwep, at hero or at monster mtarg */
STATIC_OVL void
monshoot(mtmp, otmp, mwep)
struct monst *mtmp;
struct obj *otmp, *mwep;
{
    struct monst *mtarg = target;
    int dm = distmin(mtmp->mx, mtmp->my,
                     mtarg ? mtarg->mx : mtmp->mux,
                     mtarg ? mtarg->my : mtmp->muy),
        multishot = monmulti(mtmp, otmp, mwep);

    /*
     * Caller must have called linedup() to set up tbx, tby.
     */

    if (canseemon(mtmp)) {
        const char *onm;
        char onmbuf[BUFSZ], trgbuf[BUFSZ];

        if (multishot > 1) {
            /* "N arrows"; multishot > 1 implies otmp->quan > 1, so
               xname()'s result will already be pluralized */
            Sprintf(onmbuf, "%d %s", multishot, xname(otmp));
            onm = onmbuf;
        } else {
            /* "an arrow" */
            onm = singular(otmp, xname);
            onm = obj_is_pname(otmp) ? the(onm) : an(onm);
        }
        m_shot.s = ammo_and_launcher(otmp, mwep) ? TRUE : FALSE;
        Strcpy(trgbuf, mtarg ? mon_nam(mtarg) : "");
        if (!strcmp(trgbuf, "it"))
            Strcpy(trgbuf, humanoid(mtmp->data) ? "someone" : something);
        pline("%s %s %s%s%s!", Monnam(mtmp),
              m_shot.s ? "shoots" : "throws", onm,
              mtarg ? " at " : "", trgbuf);
        m_shot.o = otmp->otyp;
    } else {
        m_shot.o = STRANGE_OBJECT; /* don't give multishot feedback */
    }
    m_shot.n = multishot;
    for (m_shot.i = 1; m_shot.i <= m_shot.n; m_shot.i++) {
        m_throw(mtmp, mtmp->mx, mtmp->my, sgn(tbx), sgn(tby), dm, otmp, TRUE);
        /* conceptually all N missiles are in flight at once, but
           if mtmp gets killed (shot kills adjacent gas spore and
           triggers explosion, perhaps), inventory will be dropped
           and otmp might go away via merging into another stack */
        if (DEADMONSTER(mtmp) && m_shot.i < m_shot.n)
            /* cancel pending shots (perhaps ought to give a message here
               since we gave one above about throwing/shooting N missiles) */
            break; /* endmultishot(FALSE); */
    }
    /* reset 'm_shot' */
    m_shot.n = m_shot.i = 0;
    m_shot.o = STRANGE_OBJECT;
    m_shot.s = FALSE;

}

/* Find a target for a ranged attack. */
struct monst *
mfind_target(mtmp)
struct monst *mtmp;
{
    int dirx[8] = {0, 1, 1,  1,  0, -1, -1, -1},
        diry[8] = {1, 1, 0, -1, -1, -1,  0,  1};

    int dir, origdir = -1;
    int x, y, dx, dy;
    int i;
    struct monst *mat, *mret = (struct monst *) 0, *oldmret = (struct monst *) 0;
    boolean conflicted = Conflict && couldsee(mtmp->mx, mtmp->my)
                                  && (distu(mtmp->mx, mtmp->my) <= (BOLT_LIM * BOLT_LIM))
                                  && !resist(mtmp, RING_CLASS, 0, 0);

    if (is_covetous(mtmp->data) && !mtmp->mtame) {
        /* find our mark and let him have it, if possible! */
        register int gx = STRAT_GOALX(mtmp->mstrategy),
                     gy = STRAT_GOALY(mtmp->mstrategy);
        register struct monst *mtmp2 = m_at(gx, gy);

        if (mtmp2 && mlined_up(mtmp, mtmp2, FALSE))
            return mtmp2;

        if (!mtmp->mpeaceful && !conflicted
            && ((mtmp->mstrategy & STRAT_STRATMASK) == STRAT_NONE)
            && lined_up(mtmp))
            return &youmonst;  /* kludge - attack the player first if possible */

        for (dir = 0; dir < 8; dir++) {
            if (dirx[dir] == sgn(gx-mtmp->mx)
                && diry[dir] == sgn(gy-mtmp->my))
                break;
        }

        if (dir == 8) {
            tbx = tby = 0;
            return 0;
        }

        origdir = -1;
    } else {
        dir = rn2(8);
        origdir = -1;

        if (!mtmp->mpeaceful && !conflicted && lined_up(mtmp))
            return &youmonst;  /* kludge - attack the player first if possible */
    }

    for (; dir != origdir; dir = ((dir + 1) % 8)) {
        if (origdir < 0)
            origdir = dir;

        mret = (struct monst *) 0;

        x = mtmp->mx;
        y = mtmp->my;
        dx = dirx[dir];
        dy = diry[dir];

        for (i = 0; i < BOLT_LIM; i++) {
            x += dx;
            y += dy;

            if (!isok(x, y) || !ZAP_POS(levl[x][y].typ)
                || closed_door(x, y))
                break; /* off the map or otherwise bad */

            if (!conflicted
                && ((mtmp->mpeaceful && (x == mtmp->mux && y == mtmp->muy))
                || (mtmp->mtame && x == u.ux && y == u.uy))) {
                mret = oldmret;
                break; /* don't attack you if peaceful */
            }

            if ((mat = m_at(x, y))) {
                /* i > 0 ensures this is not a close range attack */
                if (mtmp->mtame && !mat->mtame
                    && acceptable_pet_target(mtmp, mat, TRUE) && i > 0) {
                    if ((!oldmret)
                        || (mons[monsndx(mat->data)].difficulty >
                            mons[monsndx(oldmret->data)].difficulty)) {
                        mret = mat;
                        break;
                    }
                } else if (mm_aggression(mtmp, mat) || conflicted) {
                    if (mtmp->mtame && !conflicted
                        && !acceptable_pet_target(mtmp, mat, TRUE)) {
                        mret = oldmret;
                        break; /* not willing to attack in that direction */
                    }

                    /* Can't make some pairs work together
                       if they hate each other on principle. */
                    if ((conflicted
                         || (!(mtmp->mtame && mat->mtame) || !rn2(5)))
                        && i > 0) {
                        if ((!oldmret)
                            || (mons[monsndx(mat->data)].difficulty >
                                mons[monsndx(oldmret->data)].difficulty)) {
                            mret = mat;
                            break;
                        }
                    }
                }

                if (mtmp->mtame && mat->mtame) {
                    mret = oldmret;
                    break;  /* Not going to hit friendlies unless they
                               already hate them, as above. */
                }
            }
        }
        oldmret = mret;
    }

    if (mret != (struct monst *) 0) {
        tbx = (mret->mx - mtmp->mx);
        tby = (mret->my - mtmp->my);
        return mret; /* should be the strongest monster that's not behind
                        a friendly */
    }
    /* Nothing lined up? */
    tbx = tby = 0;
    return (struct monst *) 0;
}

/* an object launched by someone/thing other than player attacks a monster;
   return 1 if the object has stopped moving (hit or its range used up) */
int
ohitmon(mtmp, otmp, range, verbose)
struct monst *mtmp; /* accidental target, located at <bhitpos.x,.y> */
struct obj *otmp;   /* missile; might be destroyed by drop_throw */
int range;          /* how much farther will object travel if it misses;
                       use -1 to signify to keep going even after hit,
                       unless it's gone (used for rolling_boulder_traps) */
boolean verbose;    /* give message(s) even when you can't see what happened */
{
    int damage, tmp;
    boolean vis, ismimic;
    int objgone = 1;
    struct obj *mon_launcher = archer ? MON_WEP(archer) : NULL;

    notonhead = (bhitpos.x != mtmp->mx || bhitpos.y != mtmp->my);
    ismimic = M_AP_TYPE(mtmp) && M_AP_TYPE(mtmp) != M_AP_MONSTER;
    vis = cansee(bhitpos.x, bhitpos.y);

    tmp = 5 + find_mac(mtmp) + omon_adj(mtmp, otmp, FALSE);
    /* High level monsters will be more likely to hit */
    /* This check applies only if this monster is the target
     * the archer was aiming at. */
    if (archer && target == mtmp) {
        if (archer->m_lev > 5)
            tmp += archer->m_lev - 5;
        if (mon_launcher && mon_launcher->oartifact)
            tmp += spec_abon(mon_launcher, mtmp);
    }
    if (tmp < rnd(20)) {
        if (!ismimic) {
            if (vis)
                miss(distant_name(otmp, mshot_xname), mtmp);
            else if (verbose && !target)
                pline("It is missed.");
        }
        if (!range) { /* Last position; object drops */
            if (is_pole(otmp))
                return 1;
            (void) drop_throw(otmp, 0, mtmp->mx, mtmp->my);
            return 1;
        }
    } else if (otmp->oclass == POTION_CLASS) {
        if (ismimic)
            seemimic(mtmp);
        mtmp->msleeping = 0;
        if (vis)
            otmp->dknown = 1;
        /* probably thrown by a monster rather than 'other', but the
           distinction only matters when hitting the hero */
        potionhit(mtmp, otmp, POTHIT_OTHER_THROW);
        return 1;
    } else {
        damage = dmgval(otmp, mtmp);
        if (otmp->otyp == ACID_VENOM
            && (resists_acid(mtmp) || defended(mtmp, AD_ACID)))
            damage = 0; /* feedback handled further down */
#if 0 /* can't use this because we don't have the attacker */
        if (is_orc(mtmp->data) && is_elf(?magr?))
            damage++;
#endif
        if (ismimic)
            seemimic(mtmp);
        mtmp->msleeping = 0;
        if (vis) {
            if (otmp->otyp == EGG)
                pline("Splat!  %s is hit with %s egg!", Monnam(mtmp),
                      otmp->known ? an(mons[otmp->corpsenm].mname) : "an");
            else
                hit(distant_name(otmp, mshot_xname), mtmp, exclam(damage));
        } else if (verbose && !target)
            pline("%s%s is hit%s", (otmp->otyp == EGG) ? "Splat!  " : "",
                  Monnam(mtmp), exclam(damage));

        if (otmp->opoisoned && is_poisonable(otmp)) {
            if (resists_poison(mtmp) || defended(mtmp, AD_DRST)) {
                if (vis)
                    pline_The("poison doesn't seem to affect %s.",
                              mon_nam(mtmp));
            } else {
                if (rn2(30)) {
                    damage += rnd(6);
                } else {
                    if (vis)
                        pline_The("poison was deadly...");
                    damage = mtmp->mhp;
                }
            }
        }
        if (otmp->otainted && is_poisonable(otmp)) {
            if (resists_sleep(mtmp) || defended(mtmp, AD_SLEE)) {
                if (vis)
                    pline_The("drow poison doesn't seem to affect %s.",
                              mon_nam(mtmp));
            } else {
                if (!rn2(3)) {
                    damage += rnd(2);
                } else {
                    if (sleep_monst(mtmp, rn2(3) + 2, WEAPON_CLASS)) {
                        if (vis)
                            pline("%s loses consciousness.", Monnam(mtmp));
                        slept_monst(mtmp);
                    }
                }
            }
        }
        if (otmp->otyp == BARBED_NEEDLE) {
            if (resists_poison(mtmp) || defended(mtmp, AD_DRCO)) {
                if (vis)
                    pline_The("poison doesn't seem to affect %s.",
                              mon_nam(mtmp));
            } else {
                if (rn2(30)) {
                    damage += rnd(6);
                } else {
                    if (vis)
                        pline_The("poison was deadly...");
                    damage = mtmp->mhp;
                }
            }
        }
        if (!DEADMONSTER(mtmp)
            && mon_hates_material(mtmp, otmp->material)) {
            /* Extra damage is already handled in dmgval(). */
            searmsg((struct monst *) 0, mtmp, otmp, FALSE);
        }
        if (!DEADMONSTER(mtmp)
            && (otmp->oartifact
                || ((otmp->oclass == WEAPON_CLASS
                     || is_weptool(otmp) || is_bullet(otmp))
                    && otmp->oprops & ITEM_PROP_MASK))) {
            /* damage from objects with offensive object properties */
            (void) artifact_hit((struct monst *) 0, mtmp, otmp, &damage, 0);
        }

        if (ammo_stack)
            ammo_stack->oprops_known |= otmp->oprops_known;

        if (otmp->otyp == ACID_VENOM && cansee(mtmp->mx, mtmp->my)) {
            if (resists_acid(mtmp) || defended(mtmp, AD_ACID)) {
                if (vis || (verbose && !target))
                    pline("%s is unaffected.", Monnam(mtmp));
            } else {
                if (vis)
                    pline_The("%s burns %s!", hliquid("acid"), mon_nam(mtmp));
                else if (verbose && !target)
                    pline("It is burned!");
            }
        }

        if (otmp->otyp == EGG && touch_petrifies(&mons[otmp->corpsenm])) {
            if (!mtmp->mstone) {
                mtmp->mstone = 5;
                mtmp->mstonebyu = TRUE;
            }
            if (resists_ston(mtmp) || defended(mtmp, AD_STON))
                damage = 0;
        }

        if (!DEADMONSTER(mtmp)) { /* might already be dead (if petrified) */
            mtmp->mhp -= damage;
            if (DEADMONSTER(mtmp)) {
                if (vis || (verbose && !target))
                    pline("%s is %s!", Monnam(mtmp),
                          (nonliving(mtmp->data) || is_vampshifter(mtmp)
                           || !canspotmon(mtmp)) ? "destroyed" : "killed");
                /* don't blame hero for unknown rolling boulder trap */
                if (!context.mon_moving && (otmp->otyp != BOULDER
                                            || range >= 0 || otmp->otrapped))
                    xkilled(mtmp, XKILL_NOMSG);
                else
                    mondied(mtmp);
            }
        }

        /* blinding venom and cream pie do 0 damage, but verify
           that the target is still alive anyway */
        if (!DEADMONSTER(mtmp)
            && can_blnd((struct monst *) 0, mtmp,
                        (uchar) (((otmp->otyp == BLINDING_VENOM)
                                 || (otmp->otyp == SNOWBALL)) ? AT_SPIT
                                                              : AT_WEAP),
                        otmp)) {
            if (vis && mtmp->mcansee)
                pline("%s is blinded by %s.", Monnam(mtmp), the(xname(otmp)));
            mtmp->mcansee = 0;
            tmp = (int) mtmp->mblinded + rnd(25) + 20;
            if (tmp > 127)
                tmp = 127;
            mtmp->mblinded = tmp;
        }

        if (!DEADMONSTER(mtmp)
            && otmp->otyp == BALL_OF_WEBBING) {
            if (!t_at(mtmp->mx, mtmp->my)) {
                struct trap *web = maketrap(mtmp->mx, mtmp->my, WEB);
                if (web) {
                    mintrap(mtmp);
                    if (has_erid(mtmp) && mtmp->mtrapped) {
                        if (canseemon(mtmp))
                            pline("%s falls off %s %s!",
                                  Monnam(mtmp), mhis(mtmp),
                                  l_monnam(ERID(mtmp)->mon_steed));
                        separate_steed_and_rider(mtmp);
                    }
                }
            }
        }

        if (is_pole(otmp))
            return 1;

        objgone = drop_throw(otmp, 1, bhitpos.x, bhitpos.y);
        if (!objgone && range == -1) { /* special case */
            obj_extract_self(otmp);    /* free it for motion again */
            return 0;
        }
        return 1;
    }
    return 0;
}

#define MT_FLIGHTCHECK(pre,forcehit) \
    (/* missile hits edge of screen */                                  \
     !isok(bhitpos.x + dx, bhitpos.y + dy)                              \
     /* missile hits the wall */                                        \
     || IS_ROCK(levl[bhitpos.x + dx][bhitpos.y + dy].typ)               \
     /* missile hit closed door */                                      \
     || closed_door(bhitpos.x + dx, bhitpos.y + dy)                     \
     /* missile might hit iron bars */                                  \
     /* the random chance for small objects hitting bars is */          \
     /* skipped when reaching them at point blank range */              \
     || (levl[bhitpos.x + dx][bhitpos.y + dy].typ == IRONBARS           \
         && hits_bars(&singleobj,                                       \
                      bhitpos.x, bhitpos.y,                             \
                      bhitpos.x + dx, bhitpos.y + dy,                   \
                      ((pre) ? 0 : forcehit), 0))                       \
     /* Thrown objects "sink" */                                        \
     || ((!(pre) && IS_SINK(levl[bhitpos.x][bhitpos.y].typ))            \
         || IS_FORGE(levl[bhitpos.x][bhitpos.y].typ)))

void
m_throw(mon, x, y, dx, dy, range, obj, verbose)
struct monst *mon;       /* launching monster */
int x, y, dx, dy, range; /* launch point, direction, and range */
struct obj *obj;         /* missile (or stack providing it) */
register boolean verbose;
{
    struct monst *mtmp;
    struct obj *singleobj;
    boolean forcehit;
    char sym = obj->oclass;
    int hitu = 0, oldumort, blindinc = 0;

    ammo_stack = obj;

    bhitpos.x = x;
    bhitpos.y = y;
    notonhead = FALSE; /* reset potentially stale value */

    if (obj->quan == 1L) {
        /*
         * Remove object from minvent.  This cannot be done later on;
         * what if the player dies before then, leaving the monster
         * with 0 daggers?  (This caused the infamous 2^32-1 orcish
         * dagger bug).
         *
         * VENOM is not in minvent - it should already be OBJ_FREE.
         * The extract below does nothing.
         */

        /* not possibly_unwield, which checks the object's */
        /* location, not its existence */
        if (MON_WEP(mon) == obj)
            setmnotwielded(mon, obj);
        obj_extract_self(obj);
        singleobj = obj;
        obj = (struct obj *) 0;
    } else {
        singleobj = splitobj(obj, 1L);
        obj_extract_self(singleobj);
    }
    /* global pointer for missile object in OBJ_FREE state */
    thrownobj = singleobj;

    singleobj->owornmask = 0; /* threw one of multiple weapons in hand? */

    if ((singleobj->cursed || singleobj->greased) && (dx || dy) && !rn2(7)) {
        if (canseemon(mon) && flags.verbose) {
            if (is_ammo(singleobj))
                pline("%s misfires!", Monnam(mon));
            else
                pline("%s as %s throws it!", Tobjnam(singleobj, "slip"),
                      mon_nam(mon));
        }
        dx = rn2(3) - 1;
        dy = rn2(3) - 1;
        /* check validity of new direction */
        if (!dx && !dy) {
            (void) drop_throw(singleobj, 0, bhitpos.x, bhitpos.y);
            goto cleanup_thrown;
        }
    }

    if (MT_FLIGHTCHECK(TRUE, 0)) {
        /* MT_FLIGHTCHECK includes a call to hits_bars, which can end up
         * destroying singleobj and set it to null if it's any of certain
         * breakable objects like glass weapons. */
        if (singleobj) {
            (void) drop_throw(singleobj, 0, bhitpos.x, bhitpos.y);
        }
        goto cleanup_thrown;
    }
    mesg_given = 0; /* a 'missile misses' message has not yet been shown */

    /* Note: drop_throw may destroy singleobj.  Since obj must be destroyed
     * early to avoid the dagger bug, anyone who modifies this code should
     * be careful not to use either one after it's been freed.
     */
    if (sym)
        tmp_at(DISP_FLASH, obj_to_glyph(singleobj, rn2_on_display_rng));
    while (range-- > 0) { /* Actually the loop is always exited by break */
        singleobj->ox = bhitpos.x += dx;
        singleobj->oy = bhitpos.y += dy;
        mtmp = m_at(bhitpos.x, bhitpos.y);
        if (mtmp && shade_miss(mon, mtmp, singleobj, TRUE, TRUE)) {
            /* if mtmp is a shade and missile passes harmlessly through it,
               give message and skip it in order to keep going */
            mtmp = (struct monst *) 0;
        } else if (mtmp) {
            if (ohitmon(mtmp, singleobj, range, verbose))
                break;
        } else if (bhitpos.x == u.ux && bhitpos.y == u.uy) {
            if (multi)
                nomul(0);

            if (singleobj->oclass == GEM_CLASS
                && singleobj->otyp <= LAST_GEM + 9 /* 9 glass colors */
                && is_unicorn(youmonst.data)) {
                if (singleobj->otyp > LAST_GEM) {
                    You("catch the %s.", xname(singleobj));
                    You("are not interested in %s junk.",
                        s_suffix(mon_nam(mon)));
                    makeknown(singleobj->otyp);
                    dropy(singleobj);
                } else {
                    You(
                     "accept %s gift in the spirit in which it was intended.",
                        s_suffix(mon_nam(mon)));
                    (void) hold_another_object(singleobj,
                                               "You catch, but drop, %s.",
                                               xname(singleobj),
                                               "You catch:");
                }
                break;
            }
            if (singleobj->oclass == POTION_CLASS) {
                if (!Blind)
                    singleobj->dknown = 1;
                potionhit(&youmonst, singleobj, POTHIT_MONST_THROW);
                break;
            }
            oldumort = u.umortality;
            switch (singleobj->otyp) {
                int dam, hitv;
            case EGG:
                if (!touch_petrifies(&mons[singleobj->corpsenm])) {
                    impossible("monster throwing egg type %d",
                               singleobj->corpsenm);
                    hitu = 0;
                    break;
                }
            /* fall through */
            case CREAM_PIE:
            case BLINDING_VENOM:
            case SNOWBALL:
            case BALL_OF_WEBBING:
                hitu = thitu(8, 0, &singleobj, (char *) 0);
                break;
            default:
                dam = dmgval(singleobj, &youmonst);
                hitv = 3 - distmin(u.ux, u.uy, mon->mx, mon->my);
                if (hitv < -4)
                    hitv = -4;
                if (racial_elf(mon)
                    && objects[singleobj->otyp].oc_skill == P_BOW) {
                    hitv++;
                    if (MON_WEP(mon) && MON_WEP(mon)->otyp == ELVEN_BOW)
                        hitv++;
                    if (singleobj->otyp == ELVEN_ARROW)
                        dam++;
                }
                if (racial_drow(mon)
                    && objects[singleobj->otyp].oc_skill == P_BOW) {
                    hitv++;
                    if (MON_WEP(mon) && MON_WEP(mon)->otyp == DARK_ELVEN_BOW)
                        hitv++;
                    if (singleobj->otyp == DARK_ELVEN_ARROW)
                        dam++;
                }
                if (bigmonst(youmonst.data))
                    hitv++;
                hitv += 8 + singleobj->spe;
                /* M3_ACCURATE monsters get a significant bonus here */
                if ((has_erac(mon) && (ERAC(mon)->mflags3 & M3_ACCURATE))
                    || (!has_erac(mon) && is_accurate(mon->data))) {
                    hitv += mon->m_lev;
                }
                /* Drow are affected by being in both the light or the dark */
                if (racial_drow(mon)) {
                    if (!(levl[mon->mx][mon->my].lit
                          || (viz_array[mon->my][mon->mx] & TEMP_LIT))
                        || (viz_array[mon->my][mon->mx] & TEMP_DARK)) {
                        /* in darkness */
                        hitv += (mon->m_lev / 6) + 2;
                    } else {
                        /* in the light */
                        hitv -= 3;
                    }
                }
                if (dam < 1)
                    dam = 1;
                if ((maybe_polyd(is_giant(youmonst.data), Race_if(PM_GIANT)))
                    && MON_WEP(mon) && MON_WEP(mon)->otyp == SLING)
                    dam *= 2;
                hitu = thitu(hitv, dam, &singleobj, (char *) 0);
            }
            if (hitu
                && ((singleobj->opoisoned && is_poisonable(singleobj))
                    || singleobj->otyp == BARBED_NEEDLE)) {
                char onmbuf[BUFSZ], knmbuf[BUFSZ];

                Strcpy(onmbuf, xname(singleobj));
                Strcpy(knmbuf, killer_xname(singleobj));
                poisoned(onmbuf,
                         singleobj->otyp == BARBED_NEEDLE ? A_CON : A_STR,
                         knmbuf,
                         /* if damage triggered life-saving,
                            poison is limited to attrib loss */
                         (u.umortality > oldumort) ? 0 : 10, TRUE);
            }
            if (hitu && singleobj->otainted && is_poisonable(singleobj)) {
                if (how_resistant(SLEEP_RES) == 100) {
                    monstseesu(M_SEEN_SLEEP);
                    pline_The("drow poison doesn't seem to affect you.");
                } else {
                    if (!rn2(3)) {
                        losehp(resist_reduce(rnd(2), POISON_RES),
                               xname(singleobj), KILLED_BY_AN);
                    } else {
                        if (u.usleep) { /* don't let sleep effect stack */
                            losehp(resist_reduce(rnd(2), POISON_RES),
                                   xname(singleobj), KILLED_BY_AN);
                        } else {
                            You("lose consciousness.");
                            losehp(resist_reduce(rnd(2), POISON_RES),
                                   xname(singleobj), KILLED_BY_AN);
                            fall_asleep(-resist_reduce(rn2(3) + 2, SLEEP_RES), TRUE);
                        }
                    }
                }
            }
            if (hitu && can_blnd((struct monst *) 0, &youmonst,
                                 (uchar) (((singleobj->otyp == BLINDING_VENOM)
                                          || (singleobj->otyp == SNOWBALL))
                                               ? AT_SPIT
                                               : AT_WEAP),
                                 singleobj)) {
                blindinc = rnd(25);
                if (singleobj->otyp == CREAM_PIE) {
                    if (!Blind)
                        pline("Yecch!  You've been creamed.");
                    else
                        pline("There's %s sticky all over your %s.",
                              something, body_part(FACE));
                } else if (singleobj->otyp == BLINDING_VENOM
                           || singleobj->otyp == SNOWBALL) {
                    const char *eyes = body_part(EYE);

                    if (eyecount(youmonst.data) != 1)
                        eyes = makeplural(eyes);
                    /* venom in the eyes */
                    if (!Blind) {
                        if (singleobj->otyp == SNOWBALL)
                            pline_The("snowball blinds you.");
                        else
                            pline_The("venom blinds you.");
                    } else
                        Your("%s %s.", eyes, vtense(eyes, "sting"));
                }
            }
            if (hitu && singleobj->otyp == BALL_OF_WEBBING) {
                if (!t_at(u.ux, u.uy)) {
                    /* webbing won't form over liquid/lava/air */
                    struct trap *web = maketrap(u.ux, u.uy, WEB);
                    if (web) {
                        pline("%s entangles you in a web%s",
                              Monnam(mon),
                              is_giant(youmonst.data)
                                  ? ", but you rip through it!"
                                  : (webmaker(youmonst.data)
                                     || maybe_polyd(is_drow(youmonst.data),
                                                    Race_if(PM_DROW)))
                                      ? ", but you easily disentangle yourself."
                                      : "!");
                        dotrap(web, NOWEBMSG);
                        if (u.usteed && u.utrap)
                            dismount_steed(DISMOUNT_FELL);
                    }
                }
            }
            if (hitu && singleobj->otyp == EGG) {
                if (!Stoned && !Stone_resistance
                    && !(poly_when_stoned(youmonst.data)
                         && (polymon(PM_STONE_GOLEM)
                             || polymon(PM_PETRIFIED_ENT)))) {
                    make_stoned(5L, (char *) 0, KILLED_BY, "");
                }
            }
            stop_occupation();
            if (hitu) {
                (void) drop_throw(singleobj, hitu, u.ux, u.uy);
                break;
            }
        }

        forcehit = !rn2(5);
        if (!range || MT_FLIGHTCHECK(FALSE, forcehit)) {
            /* end of path or blocked */
            if (singleobj) { /* hits_bars might have destroyed it */
                /* note: pline(The(missile)) rather than pline_The(missile)
                   in order to get "Grimtooth" rather than "The Grimtooth" */
                if (range && cansee(bhitpos.x, bhitpos.y)
                    && IS_SINK(levl[bhitpos.x][bhitpos.y].typ))
                    pline("%s %s onto the sink.", The(mshot_xname(singleobj)),
                          otense(singleobj, Hallucination ? "plop" : "drop"));
                else if (range && cansee(bhitpos.x, bhitpos.y)
                         && IS_FORGE(levl[bhitpos.x][bhitpos.y].typ))
                    pline("%s %s onto the forge.", The(mshot_xname(singleobj)),
                          otense(singleobj, Hallucination ? "boop" : "drop"));
                else if (m_shot.n > 1
                         && (!mesg_given
                             || bhitpos.x != u.ux || bhitpos.y != u.uy)
                         && (cansee(bhitpos.x, bhitpos.y)
                             || (archer && canseemon(archer))))
                    pline("%s misses.", The(mshot_xname(singleobj)));

                (void) drop_throw(singleobj, 0, bhitpos.x, bhitpos.y);
            }
            break;
        }
        tmp_at(bhitpos.x, bhitpos.y);
        delay_output();
    }
    tmp_at(bhitpos.x, bhitpos.y);
    delay_output();
    tmp_at(DISP_END, 0);
    mesg_given = 0; /* reset */

    if (blindinc) {
        u.ucreamed += blindinc;
        make_blinded(Blinded + (long) blindinc, FALSE);
        if (!Blind)
            Your1(vision_clears);
    }

cleanup_thrown:
    ammo_stack = (struct obj *) 0;

    /* note: all early returns follow drop_throw() which clears thrownobj */
    thrownobj = 0;
    return;
}

#undef MT_FLIGHTCHECK

/* Monster throws item at another monster */
int
thrwmm(mtmp, mtarg)
struct monst *mtmp, *mtarg;
{
    struct obj *otmp, *mwep;
    register xchar x, y;
    boolean ispole;

    /* Polearms won't be applied by monsters against other monsters */
    if (mtmp->weapon_check == NEED_WEAPON || !MON_WEP(mtmp)) {
        mtmp->weapon_check = NEED_RANGED_WEAPON;
        /* mon_wield_item resets weapon_check as appropriate */
        if (mon_wield_item(mtmp) != 0)
            return 0;
    }

    /* Pick a weapon */
    otmp = select_rwep(mtmp);
    if (!otmp)
        return 0;
    ispole = is_pole(otmp);

    x = mtmp->mx;
    y = mtmp->my;

    mwep = MON_WEP(mtmp); /* wielded weapon */

    if (!ispole && mlined_up(mtmp, mtarg, FALSE)) {
        int chance = max(BOLT_LIM - distmin(x, y, mtarg->mx, mtarg->my), 1);

        if (!mtarg->mflee || !rn2(chance)) {
            if (ammo_and_launcher(otmp, mwep)
                && dist2(mtmp->mx, mtmp->my, mtarg->mx, mtarg->my)
                   > PET_MISSILE_RANGE2)
                return 0; /* Out of range */
            /* Set target monster */
            target = mtarg;
            archer = mtmp;
            monshoot(mtmp, otmp, mwep); /* multishot shooting or throwing */
            archer = target = (struct monst *) 0;
            nomul(0);
            return 1;
        }
    }
    return 0;
}

/* monster spits substance at monster */
int
spitmm(mtmp, mattk, mtarg)
struct monst *mtmp, *mtarg;
struct attack *mattk;
{
    struct obj *otmp;

    if (mtmp->mcan) {
        if (!Deaf) {
            if (mtmp
                && (mtmp->data == &mons[PM_SNOW_GOLEM]
                    || mtmp->data->mlet == S_SPIDER
                    || mtmp->data == &mons[PM_NEEDLE_BLIGHT]))
                ;
            else
                pline("A dry rattle comes from %s throat.",
                      s_suffix(mon_nam(mtmp)));
        }
        return 0;
    }

    if (mtmp->mspec_used)
        return 0;

    otmp = (struct obj *) 0;
    if (mlined_up(mtmp, mtarg, FALSE)) {
        switch (mattk->adtyp) {
        case AD_BLND:
        case AD_DRST:
            otmp = mksobj(BLINDING_VENOM, TRUE, FALSE);
            break;
        case AD_DRCO:
            otmp = mksobj(BARBED_NEEDLE, TRUE, FALSE);
            break;
        case AD_ACID:
            otmp = mksobj(ACID_VENOM, TRUE, FALSE);
            break;
        case AD_COLD:
            otmp = mksobj(SNOWBALL, TRUE, FALSE);
            break;
        case AD_WEBS:
            mtmp->mspec_used = rn2(4);
            otmp = mksobj(BALL_OF_WEBBING, TRUE, FALSE);
            break;
        default:
            impossible("bad attack type in spitmu");
            /*FALLTHRU*/
        }
        if (!rn2(BOLT_LIM - distmin(mtmp->mx, mtmp->my, mtarg->mx, mtarg->my))) {
            if (canseemon(mtmp)) {
                if (mtmp && mtmp->data == &mons[PM_SNOW_GOLEM])
                    pline("%s throws a snowball!", Monnam(mtmp));
                else if (mtmp && mtmp->data->mlet == S_SPIDER)
                    pline("%s shoots a ball of webbing!", Monnam(mtmp));
                else if (mtmp && mtmp->data == &mons[PM_NEEDLE_BLIGHT])
                    pline("%s fires a barbed needle!", Monnam(mtmp));
                else
                    pline("%s spits venom!", Monnam(mtmp));
            }
            target = mtarg;
            m_throw(mtmp, mtmp->mx, mtmp->my, sgn(tbx), sgn(tby),
                    distmin(mtmp->mx, mtmp->my, mtarg->mx, mtarg->my), otmp, TRUE);
            target = (struct monst *) 0;
            nomul(0);

            /* If this is a pet, it'll get hungry. Minions and
             * spell beings won't hunger */
            if (mtmp->mtame && !mtmp->isminion) {
                struct edog *dog = EDOG(mtmp);

                /* Hunger effects will catch up next move */
                if (dog->hungrytime > 1)
                    dog->hungrytime -= 5;
            }

            return 1;
        } else {
            obj_extract_self(otmp);
            obfree(otmp, (struct obj *) 0);
        }
    }
    return 0;
}

/* monster breathes at monster (ranged) */
int
breamm(mtmp, mattk, mtarg)
struct monst *mtmp, *mtarg;
struct attack  *mattk;
{
    /* if new breath types are added, change AD_STUN to max type */
    int typ = (mattk->adtyp == AD_RBRE) ? rnd(AD_STUN) : mattk->adtyp ;
    boolean player_resists = FALSE;

    if (mlined_up(mtmp, mtarg, TRUE)) {
        if (mtmp->mcan) {
            if (!Deaf) {
                if (canseemon(mtmp))
                    pline("%s coughs.", Monnam(mtmp));
                else
                    You_hear("a cough.");
            }
            return 0;
        }

        /* if we've seen the actual resistance, don't bother, or
         * if we're close by and they reflect, just jump the player */
        player_resists = m_seenres(mtmp, 1 << (typ - 1));
        if (player_resists
            || (m_seenres(mtmp, M_SEEN_REFL) != 0
                && monnear(mtmp, mtmp->mux, mtmp->muy))) {
            return 1;
        }

        if (!mtmp->mspec_used && rn2(3)) {
            if ((typ >= AD_MAGM) && (typ <= AD_STUN)) {
                if (canseemon(mtmp))
                    pline("%s breathes %s!", Monnam(mtmp), breathwep[typ - 1]);
                dobuzz((int) (-22 - (typ - 1)), (int) mattk->damn,
                       mtmp->mx, mtmp->my, sgn(tbx), sgn(tby), FALSE);
                nomul(0);
                /* breath runs out sometimes. Also, give monster some
                 * cunning; don't breath if the target fell asleep.
                 */
                mtmp->mspec_used = 6 + rn2(18);

                /* If this is a pet, it'll get hungry. Minions and
                 * spell beings won't hunger */
                if (mtmp->mtame && !mtmp->isminion) {
                    struct edog *dog = EDOG(mtmp);

                    /* Hunger effects will catch up next move */
                    if (dog->hungrytime >= 10)
                        dog->hungrytime -= 10;
                }
            } else impossible("Breath weapon %d used", typ-1);
        } else
            return 0;
    }
    return 1;
}

/* remove an entire item from a monster's inventory; destroy that item */
void
m_useupall(mon, obj)
struct monst *mon;
struct obj *obj;
{
    extract_from_minvent(mon, obj, TRUE, FALSE);
    obfree(obj, (struct obj *) 0);
}

/* remove one instance of an item from a monster's inventory */
void
m_useup(mon, obj)
struct monst *mon;
struct obj *obj;
{
    if (obj->quan > 1L) {
        obj->quan--;
        obj->owt = weight(obj);
    } else {
        m_useupall(mon, obj);
    }
}

/* monster attempts ranged weapon attack against player
 * returns TRUE if it did something; FALSE if it decided not to attack */
boolean
thrwmu(mtmp)
struct monst *mtmp;
{
    struct obj *otmp, *mwep;
    xchar x, y;
    const char *onm;

    /* Rearranged beginning so monsters can use polearms not in a line */
    if ((mtmp->weapon_check == NEED_WEAPON
        && MON_WEP(mtmp) && !is_pole(MON_WEP(mtmp))) || !MON_WEP(mtmp)) {
        if (dist2(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) <= 3) {
            mtmp->weapon_check = NEED_HTH_WEAPON;
            if (mon_wield_item(mtmp) != 0)
                return TRUE;
        }
        mtmp->weapon_check = NEED_RANGED_WEAPON;
        /* mon_wield_item resets weapon_check as appropriate */
        if (mon_wield_item(mtmp) != 0)
            return TRUE;
    }

    /* Pick a weapon */
    otmp = select_rwep(mtmp);
    if (!otmp)
        return FALSE;

    if (is_pole(otmp)) {
        int dam, hitv;

        if (otmp != MON_WEP(mtmp))
            return FALSE; /* polearm must be wielded */
        if (dist2(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy) > POLE_LIM
            || !couldsee(mtmp->mx, mtmp->my))
            return FALSE; /* Out of range, or intervening wall */

        if (canseemon(mtmp)) {
            onm = xname(otmp);
            pline("%s thrusts %s.", Monnam(mtmp),
                  obj_is_pname(otmp) ? the(onm) : an(onm));
        }

        dam = dmgval(otmp, &youmonst);
        hitv = 3 - distmin(u.ux, u.uy, mtmp->mx, mtmp->my);
        if (hitv < -4)
            hitv = -4;
        if (bigmonst(youmonst.data))
            hitv++;
        hitv += 8 + otmp->spe;
        if (dam < 1)
            dam = 1;

        (void) thitu(hitv, dam, &otmp, (char *) 0);
        stop_occupation();
        return TRUE;
    }

    x = mtmp->mx;
    y = mtmp->my;

    /* If you are coming toward the monster, the monster
     * should try to soften you up with missiles.  If you are
     * going away, you are probably hurt or running.  Give
     * chase, but if you are getting too far away, throw.
     */
    if (!lined_up(mtmp)
        || (URETREATING(x, y)
            && rn2(BOLT_LIM - distmin(x, y, mtmp->mux, mtmp->muy))))
        return FALSE;

    mwep = MON_WEP(mtmp); /* wielded weapon */
    monshoot(mtmp, otmp, mwep); /* multishot shooting or throwing */
    nomul(0);
    return TRUE;
}

/* monster spits substance at you */
int
spitmu(mtmp, mattk)
struct monst *mtmp;
struct attack *mattk;
{
    struct obj *otmp;

    if (mtmp->mcan) {
        if (!Deaf) {
            if (mtmp
                && (mtmp->data == &mons[PM_SNOW_GOLEM]
                    || mtmp->data->mlet == S_SPIDER
                    || mtmp->data == &mons[PM_NEEDLE_BLIGHT]))
                ;
            else
                pline("A dry rattle comes from %s throat.",
                      s_suffix(mon_nam(mtmp)));
        }
        return 0;
    }

    if (mtmp->mspec_used)
        return 0;

    otmp = (struct obj *) 0;
    if (lined_up(mtmp)) {
        switch (mattk->adtyp) {
        case AD_BLND:
        case AD_DRST:
            otmp = mksobj(BLINDING_VENOM, TRUE, FALSE);
            break;
        case AD_DRCO:
            otmp = mksobj(BARBED_NEEDLE, TRUE, FALSE);
            break;
        case AD_ACID:
            otmp = mksobj(ACID_VENOM, TRUE, FALSE);
            break;
        case AD_COLD:
            otmp = mksobj(SNOWBALL, TRUE, FALSE);
            break;
        case AD_WEBS:
            mtmp->mspec_used = rn2(4);
            otmp = mksobj(BALL_OF_WEBBING, TRUE, FALSE);
            break;
        default:
            impossible("bad attack type in spitmu");
            /* fall through */
            break;
        }
        if (!rn2(BOLT_LIM
                 - distmin(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy))) {
            if (canseemon(mtmp)) {
                if (mtmp && mtmp->data == &mons[PM_SNOW_GOLEM])
                    pline("%s throws a snowball!", Monnam(mtmp));
                else if (mtmp && mtmp->data->mlet == S_SPIDER)
                    pline("%s shoots a ball of webbing!", Monnam(mtmp));
                else if (mtmp && mtmp->data == &mons[PM_NEEDLE_BLIGHT])
                    pline("%s fires a barbed needle!", Monnam(mtmp));
                else
                    pline("%s spits venom!", Monnam(mtmp));
            }
            m_throw(mtmp, mtmp->mx, mtmp->my, sgn(tbx), sgn(tby),
                    distmin(mtmp->mx, mtmp->my, mtmp->mux, mtmp->muy), otmp, TRUE);
            nomul(0);
            return 0;
        } else {
            obj_extract_self(otmp);
            obfree(otmp, (struct obj *) 0);
        }
    }
    return 0;
}

/* monster breathes at you (ranged) */
int
breamu(mtmp, mattk)
struct monst *mtmp;
struct attack *mattk;
{
    /* if new breath types are added, change AD_STUN to max type */
    int typ = (mattk->adtyp == AD_RBRE) ? rnd(AD_STUN) : mattk->adtyp;

    if (lined_up(mtmp)) {
        if (mtmp->mcan) {
            if (!Deaf) {
                if (canseemon(mtmp))
                    pline("%s coughs.", Monnam(mtmp));
                else
                    You_hear("a cough.");
            }
            return 0;
        }
        if (!mtmp->mspec_used && rn2(3)) {
            if ((typ >= AD_MAGM) && (typ <= AD_STUN)) {
                if (canseemon(mtmp))
                    pline("%s breathes %s!", Monnam(mtmp),
                          breathwep[typ - 1]);
                buzz((int) (-22 - (typ - 1)), (int) mattk->damn, mtmp->mx,
                     mtmp->my, sgn(tbx), sgn(tby));
                nomul(0);
                /* breath runs out sometimes. Also, give monster some
                 * cunning; don't breath if the player fell asleep.
                 */
                if (!rn2(3))
                    mtmp->mspec_used = 10 + rn2(20);
                if (typ == AD_SLEE && how_resistant(SLEEP_RES) < 100)
                    mtmp->mspec_used += rnd(20);
            } else
                impossible("Breath weapon %d used", typ - 1);
        }
    }
    return 1;
}

/* Move from (ax,ay) to (bx,by), but only if distance is up to BOLT_LIM
   and only in straight line or diagonal, calling fnc for each step.
   Stops if fnc return TRUE, or if step was blocked by wall or closed door.
   Returns TRUE if fnc returned TRUE. */
boolean
linedup_callback(ax, ay, bx, by, fnc)
xchar ax, ay, bx, by;
boolean FDECL((*fnc), (int, int));
{
    int dx, dy;

    /* These two values are set for use after successful return. */
    tbx = ax - bx;
    tby = ay - by;

    /* sometimes displacement makes a monster think that you're at its
       own location; prevent it from throwing and zapping in that case */
    if (!tbx && !tby)
        return FALSE;

    /* straight line, orthogonal to the map or diagonal */
    if ((!tbx || !tby || abs(tbx) == abs(tby))
        && distmin(tbx, tby, 0, 0) < BOLT_LIM) {
        dx = sgn(ax - bx), dy = sgn(ay - by);
        do {
            /* <bx,by> is guaranteed to eventually converge with <ax,ay> */
            bx += dx, by += dy;
            if (!isok(bx, by))
                return FALSE;
            if (IS_ROCK(levl[bx][by].typ) || closed_door(bx, by))
                return FALSE;
            if ((*fnc)(bx, by))
                return TRUE;
        } while (bx != ax || by != ay);
    }
    return FALSE;
}

boolean
linedup(ax, ay, bx, by, boulderhandling)
register xchar ax, ay, bx, by;
int boulderhandling; /* 0=block, 1=ignore, 2=conditionally block */
{
    int dx, dy, boulderspots;

    /* These two values are set for use after successful return. */
    tbx = ax - bx;
    tby = ay - by;

    /* sometimes displacement makes a monster think that you're at its
       own location; prevent it from throwing and zapping in that case */
    if (!tbx && !tby)
        return FALSE;

    /* straight line, orthogonal to the map or diagonal */
    if ((!tbx || !tby || abs(tbx) == abs(tby))
        && distmin(tbx, tby, 0, 0) < BOLT_LIM) {
        if ((ax == u.ux && ay == u.uy) ? (boolean) couldsee(bx, by)
                                       : clear_path(ax, ay, bx, by))
            return TRUE;
        /* don't have line of sight, but might still be lined up
           if that lack of sight is due solely to boulders */
        if (boulderhandling == 0)
            return FALSE;
        dx = sgn(ax - bx), dy = sgn(ay - by);
        boulderspots = 0;
        do {
            /* <bx,by> is guaranteed to eventually converge with <ax,ay> */
            bx += dx, by += dy;
            if (IS_ROCK(levl[bx][by].typ) || closed_door(bx, by))
                return FALSE;
            if (sobj_at(BOULDER, bx, by))
                ++boulderspots;
        } while (bx != ax || by != ay);
        /* reached target position without encountering obstacle */
        if (boulderhandling == 1 || rn2(2 + boulderspots) < 2)
            return TRUE;
    }
    return FALSE;
}

boolean
mlined_up(mtmp, mdef, breath)	/* is mtmp in position to use ranged attack? */
register struct monst *mtmp;
register struct monst *mdef;
register boolean breath;
{
    struct monst *mat;
    boolean lined_up = linedup(mdef->mx, mdef->my, mtmp->mx, mtmp->my, 0);
    int dx = sgn(mdef->mx - mtmp->mx),
        dy = sgn(mdef->my - mtmp->my);
    int x = mtmp->mx, y = mtmp->my;
    int i = 10; /* arbitrary */
    /* No special checks if confused - can't tell friend from foe */
    if (!lined_up || mtmp->mconf || !mtmp->mtame)
        return lined_up;
        /* Check for friendlies in the line of fire. */
    for (; !breath || i > 0; --i) {
        x += dx;
        y += dy;
        if (!isok(x, y))
            break;
        if (x == u.ux && y == u.uy)
            return FALSE;

        if ((mat = m_at(x, y))) {
            if (!breath && mat == mdef)
                return lined_up;
            /* Don't hit friendlies: */
            if (mat->mtame)
                return FALSE;
        }
    }
    return lined_up;
}

/* is mtmp in position to use ranged attack? */
boolean
lined_up(mtmp)
register struct monst *mtmp;
{
    boolean ignore_boulders;

    /* hero concealment usually trumps monst awareness of being lined up */
    if (Upolyd && rn2(25)
        && (u.uundetected || (U_AP_TYPE != M_AP_NOTHING
                              && U_AP_TYPE != M_AP_MONSTER)))
        return FALSE;

    ignore_boulders = (racial_throws_rocks(mtmp)
                       || m_carrying(mtmp, WAN_STRIKING));
    return linedup(mtmp->mux, mtmp->muy, mtmp->mx, mtmp->my,
                   ignore_boulders ? 1 : 2);
}

/* check if a monster is carrying a particular item */
struct obj *
m_carrying(mtmp, type)
struct monst *mtmp;
int type;
{
    register struct obj *otmp;

    for (otmp = mtmp->minvent; otmp; otmp = otmp->nobj)
        if (otmp->otyp == type)
            return otmp;
    return (struct obj *) 0;
}

void
hit_bars(objp, objx, objy, barsx, barsy, breakflags)
struct obj **objp;   /* *objp will be set to NULL if object breaks */
int objx, objy;      /* hero's spot (when wielded) or missile's spot */
int barsx, barsy;    /* adjacent spot where bars are located */
unsigned breakflags; /* breakage control */
{
    struct obj *otmp = *objp;
    int obj_type = otmp->otyp;
    boolean nodissolve = ((levl[barsx][barsy].wall_info & W_NONDIGGABLE) != 0) && !Iniceq,
            your_fault = (breakflags & BRK_BY_HERO) != 0;

    if (your_fault
        ? hero_breaks(otmp, objx, objy, breakflags)
        : breaks(otmp, objx, objy)) {
        *objp = 0; /* object is now gone */
        /* breakage makes its own noises */
        if (obj_type == POT_ACID) {
            if (cansee(barsx, barsy) && !nodissolve)
                pline_The("iron bars are dissolved!");
            else
                You_hear(Hallucination ? "angry snakes!"
                                       : "a hissing noise.");
            if (!nodissolve)
                dissolve_bars(barsx, barsy);
        }
    } else if ((obj_type == LONG_SWORD && otmp->oartifact == ART_DIRGE)
               || (obj_type == AKLYS && otmp->oartifact == ART_HARBINGER)) {
        if (cansee(barsx, barsy) && !nodissolve)
            pline_The("acidic %s right through the iron bars!",
                      otmp->oartifact == ART_DIRGE ? "blade slices"
                                                   : "aklys eats");
        else
            You_hear(Hallucination ? "a hot knife slice through butter!"
                                   : "a hissing noise.");
        if (!nodissolve)
            dissolve_bars(barsx, barsy);
    } else {
        if (!Deaf)
            pline("%s!", (obj_type == BOULDER || obj_type == HEAVY_IRON_BALL)
                         ? "Whang"
                         : (otmp->oclass == COIN_CLASS
                            || objects[obj_type].oc_material == GOLD
                            || objects[obj_type].oc_material == SILVER)
                           ? "Clink"
                           : "Clonk");
    }
}

/* TRUE iff thrown/kicked/rolled object doesn't pass through iron bars */
boolean
hits_bars(obj_p, x, y, barsx, barsy, always_hit, whodidit)
struct obj **obj_p; /* *obj_p will be set to NULL if object breaks */
int x, y, barsx, barsy;
int always_hit; /* caller can force a hit for items which would fit through */
int whodidit;   /* 1==hero, 0=other, -1==just check whether it'll pass thru */
{
    struct obj *otmp = *obj_p;
    int obj_type = otmp->otyp;
    boolean hits = always_hit;

    if (!hits)
        switch (otmp->oclass) {
        case WEAPON_CLASS: {
            int oskill = objects[obj_type].oc_skill;

            hits = (oskill != -P_BOW && oskill != -P_CROSSBOW
                    && oskill != -P_DART && oskill != -P_SHURIKEN
                    && oskill != P_SPEAR);
            break;
        }
        case ARMOR_CLASS:
            hits = (objects[obj_type].oc_armcat != ARM_GLOVES);
            break;
        case TOOL_CLASS:
            hits = (obj_type != SKELETON_KEY && obj_type != LOCK_PICK
                    && obj_type != CREDIT_CARD && obj_type != TALLOW_CANDLE
                    && obj_type != WAX_CANDLE && obj_type != LENSES
                    && obj_type != PEA_WHISTLE && obj_type != MAGIC_WHISTLE
                    && obj_type != GOGGLES);
            break;
        case ROCK_CLASS: /* includes boulder */
            if (obj_type != STATUE || mons[otmp->corpsenm].msize > MZ_TINY)
                hits = TRUE;
            break;
        case FOOD_CLASS:
            if (obj_type == CORPSE && mons[otmp->corpsenm].msize > MZ_TINY)
                hits = TRUE;
            else
                hits = (is_meat_armor(otmp)
                        || obj_type == MEAT_STICK
                        || obj_type == STRIP_OF_BACON
                        || obj_type == HUGE_CHUNK_OF_MEAT);
            break;
        case SPBOOK_CLASS:
        case WAND_CLASS:
        case BALL_CLASS:
        case CHAIN_CLASS:
            hits = TRUE;
            break;
        default:
            break;
        }

    if (hits && whodidit != -1) {
        hit_bars(obj_p, x, y, barsx, barsy,
                 (whodidit == 1) ? BRK_BY_HERO : 0);
    }

    return hits;
}

/*mthrowu.c*/
