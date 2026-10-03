#ifndef TIMER_H
#define TIMER_H

/* =========================================================
   TIMER.H — Système de timer haute résolution
   =========================================================
   Le PC dispose d'un circuit appelé PIT (Programmable
   Interval Timer, chip Intel 8253/8254) qui génère des
   interruptions matérielles à intervalles réguliers.

   Par défaut le BIOS programme le PIT à environ 18,2 Hz
   (une interruption toutes les 55 ms). C'est trop lent
   pour animer des scènes fluides.

   Ce module reprogramme le PIT canal 0 à TARGET_HZ (70 Hz)
   et installe sa propre ISR (Interrupt Service Routine)
   sur le vecteur 08h. Pour ne pas casser les fonctions DOS
   qui dépendent du timer BIOS (horloge, délais...), on
   chaîne l'ISR originale à la fréquence correcte grâce
   à un accumulateur (voir timer.c).
   ========================================================= */

/* ---------------------------------------------------------
   Constantes de fréquence
   --------------------------------------------------------- */

/* Fréquence de l'oscillateur interne du PIT en Hz.
   C'est une constante matérielle fixe sur tous les PC. */
#define PIT_FREQ  1193180UL

/* Fréquence cible de notre timer en Hz.
   70 Hz = environ 14,3 ms par tick, ce qui donne une
   animation fluide et correspond à la fréquence VGA. */
#define TARGET_HZ 70

/* Valeur à charger dans le PIT pour obtenir TARGET_HZ.
   Le PIT divise sa fréquence d'entrée par ce diviseur.
   Ex : 1193180 / 70 = 17045,4 -> 17045 (division entière,
   tronquée), soit 70,002 Hz réels. */
#define DIVISOR   (PIT_FREQ / TARGET_HZ)

/* ---------------------------------------------------------
   Variable globale
   --------------------------------------------------------- */

/* Compteur de ticks incrémenté à chaque interruption timer.
   Déclaré volatile car modifié par une ISR (hors du flux
   normal du programme) : empêche le compilateur de le
   mettre en cache dans un registre. */
extern volatile unsigned long timer_ticks;

/* ---------------------------------------------------------
   Fonctions
   --------------------------------------------------------- */

/* Installe notre ISR sur le vecteur 08h et reprogramme
   le PIT à TARGET_HZ. À appeler une seule fois au démarrage,
   avant toute utilisation du timer. */
void installTimer(void);

/* Restaure l'ISR BIOS originale et remet le PIT à 18,2 Hz.
   À appeler impérativement avant de quitter le programme,
   sinon DOS sera déréglé après la sortie. */
void restoreTimer(void);

/* Retourne la valeur actuelle de timer_ticks.
   Préférer cette fonction à l'accès direct à timer_ticks
   pour éventuellement ajouter une section critique plus tard. */
unsigned long readTimer(void);

/* Calcule le nombre de ticks écoulés entre start et stop.
   Gère correctement le débordement du compteur 32 bits
   (qui se produit après 2^32 / 70 ≈ 61,4 millions de
   secondes, soit environ 710 jours ≈ 1 an et 11 mois
   d'exécution continue à 70 Hz). */
unsigned long elapsedTime(unsigned long start, unsigned long stop);

/* Convertit un intervalle de ticks en millisecondes.
   Formule : ticks * 1000 / TARGET_HZ. */
unsigned long elapsedTimeMs(unsigned long start, unsigned long stop);

/* Attend le nombre de millisecondes demandé en bouclant
   sur timer_ticks. Plus précis que la fonction delay()
   standard qui utilise le timer BIOS à 18,2 Hz. */
void pause(unsigned long ms);

#endif /* TIMER_H */
