/*--------------------------------------------------------------------------
 * Programme à compléter pour écrire un mini-shell multi-jobs avec tubes
 * -----------------------------------------------------------------------*/

/*--------------------------------------------------------------------------
 * headers à inclure afin de pouvoir utiliser divers appels systèmes
 * -----------------------------------------------------------------------*/
#include <stdio.h>     // pour printf() and co
#include <stdlib.h>    // pour exit()
#include <errno.h>     // pour errno and co
#include <unistd.h>    // pour fork()
#include <sys/types.h> // pour avoir pid_t
#include <sys/wait.h>  // pour avoir wait() and co
#include <signal.h>    // pour sigaction()
#include <string.h>    // pour strrchr()

/*--------------------------------------------------------------------------
 * header à inclure pour les constantes et types spéciaux
 * -----------------------------------------------------------------------*/
#include "mini_shell.h"

/*--------------------------------------------------------------------------
 * header à inclure afin de pouvoir utiliser le parser de ligne de commande
 * -----------------------------------------------------------------------*/
#include "analyse_ligne.h" // pour avoir extrait_commandes()

/*--------------------------------------------------------------------------
 * header à inclure afin de pouvoir utiliser la gestion des jobs
 * -----------------------------------------------------------------------*/
#include "jobs.h" // pour avoir traite_[kill,stop]() + *job*()

/*--------------------------------------------------------------------------
 * header à inclure afin de pouvoir exécuter des commandes externes
 * -----------------------------------------------------------------------*/
#include "externes.h" // pour avoir executer_commandes()

/*--------------------------------------------------------------------------
 * header à inclure afin de pouvoir utiliser nos propres commandes internes
 * -----------------------------------------------------------------------*/
#include "internes.h" // pour avoir commande_interne()

/*-------------------------------------------------------------------------------
 * Macro pour éviter le warning "unused parameter" dans une version intermédiaire
 * -----------------------------------------------------------------------------*/
//#define UNUSED(x) (void)(x)

/*--------------------------------------------------------------------------
 * Variable globale nécessaire pour l'utiliser dans traite_signal()
 * -----------------------------------------------------------------------*/
static job_set_t g_mes_jobs;               // pour la gestion des jobs

// Déclaration d'une fonction utilisée dans traite_signal() mais définie plus bas.
static void affiche_invite(void);
/*--------------------------------------------------------------------------
 * fonction qui traite les fils morts
 * -----------------------------------------------------------------------*/
static int traite_fils_mort(int pid)
{
  int found = 0; // Valeur de retour par défaut
  for (int i=0; i<NB_MAX_JOBS; i++) { // On parcours le tableau des jobs
    for (int id=0; id<NB_MAX_COMMANDES; id++) { // On parcours le tableau des fils du job
      if (g_mes_jobs.jobs[i].pids[id]==pid) { // Si on trouve le fils
        g_mes_jobs.jobs[i].pids[id] = 0; // On remet le pid a 0
        found = 1; // On l'a trouvé
        g_mes_jobs.jobs[i].nb_restants--; // On decremente le nombre de fils restants
      }
    }
    if (g_mes_jobs.jobs[i].nb_restants==0) { // Si il ne reste plus de fils vivants dans le job
      for (int id=0; id<NB_MAX_COMMANDES; id++) { // On parcours le tableau des fils morts
        g_mes_jobs.jobs[i].pids[id]=-2; // On les remets tous les fils du job a -2
      }
      if (i==g_mes_jobs.job_fg) { // Si c'est le fils en avant plan
        g_mes_jobs.job_fg=-2; // On mets l'id du fils en avant plan a -2
      }
    }
  }
  return found;
}

/*--------------------------------------------------------------------------
 * handler qui traite les signaux
 * -----------------------------------------------------------------------*/
static void traite_signal(int signal_recu)
{
  if (signal_recu==SIGINT) { // Ctrl+C
    /*
    printf("\n");
    affiche_invite();
    */
    action_job(g_mes_jobs.job_fg,g_mes_jobs.jobs[g_mes_jobs.job_fg],SIGKILL,"");
  } else if (signal_recu==SIGTSTP) { // Ctrl+Z
    /*
    printf("\n");
    affiche_invite();
    */
    action_job(g_mes_jobs.job_fg,g_mes_jobs.jobs[g_mes_jobs.job_fg],SIGSTOP,"");
  } else if (signal_recu==SIGCHLD) { // Fils mort
    int res_w;
    res_w = (int)wait(NULL); // On recupére le fils (et son pid)
    if (res_w==-1) {perror("Echec wait"); exit(errno);} // On attends le fils pour eviter qu'il reste zombie
    if (traite_fils_mort(res_w)==0) {perror("Fils inconnu"); exit(errno);} // On traite les fils mort
  } else {
    printf("Signal inattendu (%d)\n",signal_recu);
  }
}

/*--------------------------------------------------------------------------
 * fonction d'initialisation de la structure de contrôle des signaux
 * -----------------------------------------------------------------------*/
static void initialiser_gestion_signaux(struct sigaction *sig)
{
  sig->sa_handler=traite_signal;
  sigaction(SIGINT,sig,NULL); // On enregistre le handler pour Ctrl+C
  sigaction(SIGTSTP,sig,NULL); // On enregistre le handler pour Ctrl+Z
  sigaction(SIGCHLD,sig,NULL); // On enregister le handler pour traiter les fils morts
}

/*--------------------------------------------------------------------------
 * fonction d'affichage du prompt
 * -----------------------------------------------------------------------*/
static void affiche_invite(void)
{
   // TODO à modifier : insérer le nom du répertoire courant
   char* nom_complet_dir = get_current_dir_name(); // Retourne le chemin absolu du repertoire courant
   char* nom_dir = strrchr(nom_complet_dir,'/'); // Recupère le nom du dossier courant precedé d'un '/'

   strncpy(nom_dir,nom_dir+1,strlen(nom_dir)-1); // On enlève le '/' (déplace la chaine)
   nom_dir[strlen(nom_dir)-1]='\0'; // On enleve le dernier charactere

   printf("%s> ",nom_dir);
   fflush(stdout); // Force a afficher le buffer de texte
   free(nom_complet_dir);
}

/*--------------------------------------------------------------------------
 * Analyse de la ligne de commandes et
 * exécution des commandes de façon concurrente
 * -----------------------------------------------------------------------*/
static void execute_ligne(ligne_analysee_t *ligne_analysee, job_set_t *mes_jobs, struct sigaction *sig)
{
   job_t *j;

   FILE* info = fopen("/dev/pts/1","w");
   fprintf(info,"Test 1\n");
   // on extrait les commandes présentes dans la ligne de commande
   // et l'on détermine si elle doit être exécutée en avant-plan
   int isfg=extrait_commandes(ligne_analysee);
   fprintf(info,"Test 2\n");

   /*
   // On redéfini le Ctrl+C afin qu'il ne puisse pas stopper le père
   sig->sa_handler=SIG_IGN;
   sigaction(SIGINT,sig,NULL);
   */

   // s'il ne s'agit pas d'une commande interne au shell,
   // la ligne est exécutée par un ou des fils
   if (! commande_interne(ligne_analysee,mes_jobs) ) {
     fprintf(info,"Test 3\n");
    // trouve l'adresse d'une structure libre pour lui associer le job à exécuter
    j=preparer_nouveau_job(isfg,ligne_analysee->ligne,mes_jobs);
    fprintf(info,"Test 3bis\n");

     // fait exécuter les commandes de la ligne par des fils
     executer_commandes(j,ligne_analysee, sig);
     fprintf(info,"Test NumeroBis\n");

  }
  fprintf(info,"Test 4\n");
  /*
//  initialiser_gestion_signaux(sig);
  sig->sa_handler=traite_signal;
  sigaction(SIGINT,sig,NULL);
  */
  // ménage
  *ligne_analysee->ligne='\0';
   fclose(info);
}

/*--------------------------------------------------------------------------
 * Fonction principale du mini-shell
 * -----------------------------------------------------------------------*/
int main(void) {
   ligne_analysee_t m_ligne_analysee;  // pour l'analyse d'une ligne de commandes
   struct sigaction m_sig;             // structure sigaction pour gérer les signaux
   m_sig.sa_flags=0;
   sigemptyset(&m_sig.sa_mask);


   // initialise les structures de contrôle des jobs
   initialiser_jobs(&g_mes_jobs);

   // initialise la structure de contrôle des signaux
   initialiser_gestion_signaux(&m_sig);

   while(1)
   {
      affiche_invite();
      lit_ligne(&m_ligne_analysee);
      execute_ligne(&m_ligne_analysee,&g_mes_jobs,&m_sig);
   }
   return EXIT_SUCCESS;
}
