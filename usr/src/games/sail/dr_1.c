#ifndef lint
static	char *sccsid = "@(#)dr_1.c	2.6 83/12/17";
#endif

#include "driver.h"

main(argc, argv)
int argc;
char **argv;
{
	register int n;
	register struct ship *sp;
	int nat[NNATION];

	if (argc != 2)
		exit(1);
	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGQUIT, SIG_IGN);
	(void) signal(SIGTSTP, SIG_IGN);
	(void) srand(getpid());
	/* ;;; add code here to check the game number. */
	game = atoi(argv[1]);
	cc = &scene[game];
	ls = &cc->ship[cc->vessels];
	if (sync_open() < 0) {
		perror("driver: syncfile");
		exit(1);
	}
	for (n = 0; n < NNATION; n++)
		nat[n] = 0;
	foreachship(sp) {
		sp->file = (struct File *) calloc(1, sizeof (struct File));
		if (sp == NULL) {
			(void) printf("driver: OUT OF MEMORY\n");
			exit(0);
		}
		sp->file->index = sp - SHIP(0);
		sp->file->loadL = L_ROUND;
		sp->file->loadR = L_ROUND;
		sp->file->readyR = R_LOADED|R_INITIAL;
		sp->file->readyL = R_LOADED|R_INITIAL;
		sp->file->stern = nat[sp->nationality]++;
		sp->file->dir = sp->shipdir;
		sp->file->row = sp->shiprow;
		sp->file->col = sp->shipcol;
	}
	windspeed = cc->windspeed;
	winddir = cc->winddir;
	for (;;) {
		sleep(7);
		Sync();
		next();
		unfoul();
		checkup();
		prizecheck();
		moveall();
		thinkofgrapples();
		boardcomp();
		compcombat();
		resolve();
		reload();
		checksails();
		Sync();
	}
}

unfoul()
{
	register struct ship *sp;
	struct ship *to;
	register int nat;
	register i;

	foreachship(sp) {
		if (sp->file->captain[0])
			continue;
		nat = capship(sp)->nationality;
		foreachship(to) {
			if (nat != capship(to)->nationality
			    && !toughmelee(sp, to, 0, 0))
				continue;
			for (i = fouled2(sp, to); --i >= 0;)
				if (die() <= 2)
					cleanfoul(sp, to, 0);
		}
	}
}

boardcomp()
{
	int crew[3];
	register struct ship *sp, *sq;

	foreachship(sp) {
		if (*sp->file->captain)
			continue;
		if (sp->file->dir == 0)
			continue;
		if (sp->file->struck || sp->file->captured != 0)
			continue;
		if (!snagged(sp))
			continue;
		crew[0] = sp->specs->crew1 != 0;
		crew[1] = sp->specs->crew2 != 0;
		crew[2] = sp->specs->crew3 != 0;
		foreachship(sq) {
			if (!Xsnagged2(sp, sq))
				continue;
			if (meleeing(sp, sq))
				continue;
			if (!sq->file->dir
				|| sp->nationality == capship(sq)->nationality)
				continue;
			switch (sp->specs->class - sq->specs->class) {
			case -3: case -4: case -5:
				if (crew[0]) {
					/* OBP */
					sendbp(sp, sq, crew[0]*100, 0);
					crew[0] = 0;
				} else if (crew[1]){
					/* OBP */
					sendbp(sp, sq, crew[1]*10, 0);
					crew[1] = 0;
				}
				break;
			case -2:
				if (crew[0] || crew[1]) {
					/* OBP */
					sendbp(sp, sq, crew[0]*100+crew[1]*10,
						0);
					crew[0] = crew[1] = 0;
				}
				break;
			case -1: case 0: case 1:
				if (crew[0]) {
					/* OBP */
					sendbp(sp, sq, crew[0]*100+crew[1]*10,
						0);
					crew[0] = crew[1] = 0;
				}
				break;
			case 2: case 3: case 4: case 5:
				/* OBP */
				sendbp(sp, sq, crew[0]*100+crew[1]*10+crew[2],
					0);
				crew[0] = crew[1] = crew[2] = 0;
				break;
			}
		}
	}
}

fightitout(from, to, key)
struct ship *from, *to;
int key;
{
	struct ship *fromcap, *tocap;
	int crewfrom[3], crewto[3], menfrom, mento;
	int pcto, pcfrom, fromstrength, strengthto, frominjured, toinjured;
	int topoints;
	int index, totalfrom = 0, totalto = 0;
	int count;
	char message[60];

	menfrom = mensent(from, to, crewfrom, &fromcap, &pcfrom, key);
	mento = mensent(to, from, crewto, &tocap, &pcto, 0);
	if (fromcap == 0)
		fromcap = from;
	if (tocap == 0)
		tocap = to;
	if (key) {
		if (!menfrom) {		 /* if crew surprised */
			if (fromcap == from)
				menfrom = from->specs->crew1
					+ from->specs->crew2
					+ from->specs->crew3;
			else
				menfrom = from->file->pcrew;
		} else {
			menfrom *= 2;	/* DBP's fight at an advantage */
		}
	}
	fromstrength = menfrom * fromcap->specs->qual;
	strengthto = mento * tocap->specs->qual;
	for (count = 0;
	     (fromstrength < strengthto * 3 && strengthto < fromstrength * 3
	      || fromstrength == -1) && count < 4;
	     count++) {
		index = fromstrength/10;
		if (index > 8)
			index = 8;
		toinjured = MT[index][2 - die() / 3];
		totalto += toinjured;
		index = strengthto/10;
		if (index > 8)
			index = 8;
		frominjured = MT[index][2 - die() / 3];
		totalfrom += frominjured;
		menfrom -= frominjured;
		mento -= toinjured;
		fromstrength = menfrom * fromcap->specs->qual;
		strengthto = mento * tocap->specs->qual;
	}
	if (fromstrength >= strengthto * 3 || count == 4) {
		unboard(to, from, 0);
		subtract(from, totalfrom, crewfrom, fromcap, pcfrom);
		subtract(to, totalto, crewto, tocap, pcto);
		makesignal(from, "boarders from %s repelled", to);
		(void) sprintf(message, "killed in melee: %d.  %s: %d",
			totalto, from->shipname, totalfrom);
		Write(W_SIGNAL, to, 1, (int) message, 0, 0, 0);
		if (key)
			return 1;
	} else if (strengthto >= fromstrength * 3) {
		unboard(from, to, 0);
		subtract(from, totalfrom, crewfrom, fromcap, pcfrom);
		subtract(to, totalto, crewto, tocap, pcto);
		if (key) {
			if (fromcap != from)
				Write(W_POINTS, fromcap, 0,
					fromcap->file->points -
						from->file->struck
						? from->specs->pts
						: 2 * from->specs->pts,
					0, 0, 0);

/* ptr1 points to the shipspec for the ship that was just unboarded.
   I guess that what is going on here is that the pointer is multiplied
   or something. */

			Write(W_CAPTURED, from, 0, to->file->index, 0, 0, 0);
			topoints = 2 * from->specs->pts + to->file->points;
			if (from->file->struck)
				topoints -= from->specs->pts;
			Write(W_POINTS, to, 0, topoints, 0, 0, 0);
			mento = crewto[0] ? crewto[0] : crewto[1];
			if (mento) {
				subtract(to, mento, crewto, tocap, pcto);
				subtract(from, - mento, crewfrom, to, 0);
			}
			(void) sprintf(message, "captured by the %s!",
				to->shipname);
			Write(W_SIGNAL, from, 1, (int) message, 0, 0, 0);
			(void) sprintf(message, "killed in melee: %d.  %s: %d",
				totalto, from->shipname, totalfrom);
			Write(W_SIGNAL, to, 1, (int) message, 0, 0, 0);
			mento = 0;
			return 0;
		}
	}
	return 0;
}

resolve()
{
	int thwart;
	register struct ship *sp, *sq;

	foreachship(sp) {
		if (sp->file->dir == 0)
			continue;
		for (sq = sp + 1; sq < ls; sq++)
			if (sq->file->dir && meleeing(sp, sq) && meleeing(sq, sp))
				(void) fightitout(sp, sq, 0);
		thwart = 2;
		foreachship(sq) {
			if (sq->file->dir && meleeing(sq, sp))
				thwart = fightitout(sp, sq, 1);
			if (!thwart)
				break;
		}
		if (!thwart) {
			foreachship(sq) {
				if (sq->file->dir && meleeing(sq, sp))
					unboard(sq, sp, 0);
				unboard(sp, sq, 0);
			}
			unboard(sp, sp, 1);
		} else if (thwart == 2)
			unboard(sp, sp, 1);
	}
}

compcombat()
{
	register n;
	register struct ship *sp;
	struct ship *closest;
	int crew[3], men = 0, target, temp;
	int r, guns, ready, load, car;
	int index, rakehim, sternrake;
	int shootat, hit;

	foreachship(sp) {
		if (sp->file->captain[0] || sp->file->dir == 0)
			continue;
		crew[0] = sp->specs->crew1;
		crew[1] = sp->specs->crew2;
		crew[2] = sp->specs->crew3;
		for (n = 0; n < 3; n++) {
			if (sp->file->OBP[n].turnsent)
				men += sp->file->OBP[n].mensent;
		}
		for (n = 0; n < 3; n++) {
			if (sp->file->DBP[n].turnsent)
				men += sp->file->DBP[n].mensent;
		}
		if (men){
			crew[0] = men/100 ? 0 : crew[0] != 0;
			crew[1] = (men%100)/10 ? 0 : crew[1] != 0;
			crew[2] = men%10 ? 0 : crew[2] != 0;
		}
		for (r = 0; r < 2; r++) {
			if (!crew[2])
				continue;
			if (sp->file->struck)
				continue;
			if (r) {
				ready = sp->file->readyR;
				guns = sp->specs->gunR;
				car = sp->specs->carR;
			} else {
				ready = sp->file->readyL;
				guns = sp->specs->gunL;
				car = sp->specs->carL;
			}
			if (!guns && !car)
				continue;
			if ((ready & R_LOADED) == 0)
				continue;
			closest = closestenemy(sp, r ? 'r' : 'l', 0);
			if (closest == 0)
				continue;
			if (range(closest, sp) > range(sp, closestenemy(sp, r ? 'r' : 'l', 1)))
				continue;
			if (closest->file->struck)
				continue;
			target = range(sp, closest);
			if (target > 10)
				continue;
			if (!guns && target >= 3)
				continue;
			load = L_ROUND;
			if (target == 1 && sp->file->loadwith == L_GRAPE)
				load = L_GRAPE;
			if (target <= 3 && closest->file->FS)
				load = L_CHAIN;
			if (target == 1 && load != L_GRAPE)
				load = L_DOUBLE;
			if (load > L_CHAIN && target < 6)
				shootat = HULL;
			else
				shootat = RIGGING;
			rakehim = gunsbear(sp, closest)
				&& !gunsbear(closest, sp);
			temp = portside(closest, sp, 1)
				- closest->file->dir + 1;
			if (temp < 1)
				temp += 8;
			if (temp > 8)
				temp -= 8;
			sternrake = temp > 4 && temp < 6;
			index = guns;
			if (target < 3)
				index += car;
			index = (index - 1) / 3;
			index = index > 8 ? 8 : index;
			if (!rakehim)
				hit = HDT[index][target-1];
			else
				hit = HDTrake[index][target-1];
			if (rakehim && sternrake)
				hit++;
			hit += QUAL[index][capship(sp)->specs->qual - 1];
			for (n = 0; n < 3 && sp->file->captured == 0; n++)
				if (!crew[n])
					if (index <= 5)
						hit--;
					else
						hit -= 2;
			if (ready & R_INITIAL) {
				if (!r)
					sp->file->readyL &= ~R_INITIAL;
				else
					sp->file->readyR &= ~R_INITIAL;
				if (index <= 3)
					hit++;
				else
					hit += 2;
			}
			if (sp->file->captured != 0)
				if (index <= 1)
					hit--;
				else
					hit -= 2;
			hit += AMMO[index][load - 1];
			temp = sp->specs->class;
			if ((temp >= 5 || temp == 1) && windspeed == 5)
				hit--;
			if (windspeed == 6 && temp == 4)
				hit -= 2;
			if (windspeed == 6 && temp <= 3)
				hit--;
			if (hit >= 0) {
				if (load != L_GRAPE)
					hit = hit > 10 ? 10 : hit;
				table(shootat, load, hit, closest, sp, die());
			}
		}
	}
}

next()
{
	if (++turn % 55 == 0)
		if (alive)
			alive = 0;
		else
			people = 0;
	if (people <= 0 || windspeed == 7) {
		sync_close(1);
		exit(0);
	}
	Write(W_TURN, SHIP(0), 0, turn, 0, 0, 0);
	if (turn % 7 == 0) {
		if (die() >= cc->windchange || !windspeed) {
			switch (die()) {
			case 1:
				winddir = 1;
				break;
			case 2:
				break;
			case 3:
				winddir++;
				break;
			case 4:
				winddir--;
				break;
			case 5:
				winddir += 2;
				break;
			case 6:
				winddir -= 2;
				break;
			}
			if (winddir > 8)
				winddir -= 8;
			if (winddir < 1)
				winddir += 8;
			if (windspeed)
				switch (die()) {
				case 1:
				case 2:
					windspeed--;
					break;
				case 5:
				case 6:
					windspeed++;
					break;
				}
			else
				windspeed++;
			Write(W_WIND, SHIP(0), 0, winddir, windspeed, 0, 0);
		}
	}
}

/*ARGSUSED*/
/*VARARGS2*/
Signal(fmt, ship, a, b, c)
char *fmt;
struct ship *ship;
{
}
