#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <termios.h>

struct sigaction prije;

void obradi_dogadjaj(int sig)
{
	printf("\n[signal SIGINT] proces %d primio signal %d\n", (int) getpid(), sig);
	//proslijedi ga ako se program izvodi u prvom planu
}

void obradi_signal_zavrsio_neki_proces_dijete(int id)
{
	//ako je već dole pozvan waitpid, onda na ovaj signal waitpid ne daje informaciju (ponovo)
	pid_t pid_zavrsio = waitpid(-1, NULL, WNOHANG); //ne čeka
	if (pid_zavrsio > 0)
		if (kill(pid_zavrsio, 0) == -1) //možda je samo promijenio stanje ili je bas završio
			printf("\n[roditelj %d - SIGCHLD + waitpid] dijete %d zavrsilo s radom\n", (int) getpid(), pid_zavrsio);
	//else
		//printf("\n[roditelj %d - SIGCHLD + waitpid] waitpid ne daje informaciju\n", (int) getpid());
}


//primjer stvaranja procesa i u njemu pokretanja programa
pid_t pokreni_program(char *naredba[], int u_pozadini)
{
	pid_t pid_novi;
	if ((pid_novi = fork()) == 0) {
		printf("[dijete %d] krenuo s radom\n", (int) getpid());
		sigaction(SIGINT, &prije, NULL); //resetiraj signale
		setpgid(pid_novi, pid_novi); //stvori novu grupu za ovaj proces
		if (!u_pozadini)
			tcsetpgrp(STDIN_FILENO, getpgid(pid_novi)); //dodijeli terminal

		execvp(naredba[0], naredba);
		perror("Nisam pokrenuo program!");
		exit(1);
	}

	return pid_novi; //roditelj samo dolazi do tuda
}

int main()
{
	struct sigaction act;
	pid_t pid_novi;
	//ostale varijable su definirane neposredno prije koristenja
	//ali SAMO RADI jednostavnijeg praćenja
	//uobicajeno se sve varijable deklariraju ovdje!

	printf("[roditelj %d] krenuo s radom\n", (int) getpid());

	//postavi signale SIGINT i SIGCHLD
	act.sa_handler = obradi_dogadjaj;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, &prije);
	act.sa_handler = obradi_signal_zavrsio_neki_proces_dijete;
	sigaction(SIGCHLD, &act, NULL);
	act.sa_handler = SIG_IGN;
	sigaction(SIGTTOU, &act, NULL); //zbog tcsetpgrp

	struct termios shell_term_settings;
	tcgetattr(STDIN_FILENO, &shell_term_settings);

	//primjer stvaranja procesa i u njemu pokretanja programa
	char *naredba_echo[] = {"echo", "-e", "Jedan\nDva\nTri", NULL};
	pid_novi = pokreni_program(naredba_echo, 0);
	waitpid(pid_novi, NULL, 0); //čekaj da završi

	//uzmi natrag kontrolu nad terminalom
	tcsetpgrp(STDIN_FILENO, getpgid(0));
	
	size_t vel_buf = 128;
	char buffer[vel_buf];

	do {
		//unos teksta i parsiranje
		printf("[roditelj] unesi naredbu: ");

		if (fgets(buffer, vel_buf, stdin) != NULL) {
			#define MAXARGS 5
			char *argv[MAXARGS];
			int argc = 0;
			argv[argc] = strtok(buffer, " \t\n");
			while (argv[argc] != NULL) {
				argc++;
				argv[argc] = strtok(NULL, " \t\n");
			}

			//pokretanje "naredbe" (pretpostavljam da je program)
			printf("[roditelj] pokrecem program\n");
			pid_novi = pokreni_program(argv, 0);

			printf("[roditelj] cekam da zavrsi\n");
			pid_t pid_zavrsio;
			do {
				pid_zavrsio = waitpid(pid_novi, NULL, 0); //čekaj
				if (pid_zavrsio > 0) {
					if (kill(pid_novi, 0) == -1) { //nema ga više? ili samo mijenja stanje
						printf("[roditelj] dijete %d zavrsilo s radom\n", pid_zavrsio);

						//vraćam terminal ljusci
						tcsetpgrp(STDIN_FILENO, getpgid(0));
						tcsetattr(STDIN_FILENO, 0, &shell_term_settings);
					}
					else {
						pid_novi = (pid_t) 0; //nije gotov
					}
				}
				else {
					printf("[roditelj] waitpid gotov ali ne daje informaciju\n");
					break;
				}
			}
			while(pid_zavrsio <= 0);
		}
		else {
			//printf("[roditelj] neka greska pri unosu, vjerojatno dobio signal\n");
		}
	}
	while(strncmp(buffer, "exit", 4) != 0);

	return 0;
}

/* primjer pokretanja:

$ gcc lab2-sucelja.c
$ ./a.out
[roditelj 1621] krenuo s radom
[dijete 1622] krenuo s radom
Jedan
Dva
Tri
[roditelj] unesi naredbu: sleep 5
[roditelj] pokrecem program
[roditelj] cekam da zavrsi
[dijete 1731] krenuo s radom
[roditelj] dijete 1731 zavrsilo s radom
[roditelj] unesi naredbu: bc -q
[roditelj] pokrecem program
[roditelj] cekam da zavrsi
[dijete 1750] krenuo s radom
123-2*75+111
84
quit
[roditelj] dijete 1750 zavrsilo s radom
[roditelj] unesi naredbu: exit
[roditelj] pokrecem program
[roditelj] cekam da zavrsi
[dijete 1787] krenuo s radom
Nisam pokrenuo program!: No such file or directory
[roditelj] dijete 1787 zavrsilo s radom
$

*/