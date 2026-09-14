/**
 * @file readcmd.h
 * @author Guillaume Dupont <guillaume.dupont@irit.fr>
 * @version 1.0
 * @brief Module `readcmd` pour lire une chaîne de caractère et en extraire les
 * composantes, façon shell.
 *
 *
 * @details Module principal pour faire de la lecture de commande. Le module n'est
 * probablement pas exhaustif mais, bien utilisé, il permet de répondre à au
 * moins 95% des besoins liés au traitement des commandes.
 *
 * Syntaxiquement, une commande est un ensemble de groupes de caractères (des
 * tokens), séparés par des espaces :
 *      `<token1> <token2> <token3> ... <tokenn>`
 *
 * Chaque token est soit:
 *   - un symbole spécial (`&`, `|`, `>`, `<`)
 *   - une suite de symboles (potentiellement échappés)
 *   - une suite de symboles quelconques entre guillemets (") incluant le
 *   guillemet échappé (\")
 * 
 * Par ex:
 *   - `abc def ghi`   -> trois tokens, `"abc"`, `"def"` et `"ghi"`
 *   - `abc\ def ghi`  -> deux tokens, `"abc def"` et `"ghi"̀` 
 *   - `"toto titi & tutu" tata` -> deux tokens, `"toto titi & tutu"` et `"tata"`
 *   - `ps > test.txt` -> trois tokens `"ps"`, `">"` et `"test.txt"`
 *
 * Les tokens composés d'un symbole spécial sont appelés token spéciaux. Il en
 * existe 4:
 *   - Le '&' désigne une commande à passer en arrière plan (comme en bash). Le
 *   '&' doit se trouver à la fin de la chaîne de commandes (la ligne
 *   "abc & | def" est invalide par exemple)
 *   - Le '>' et le '<' désignent (resp.) la redirection vers et depuis un
 *   fichier. Les redirections sont optionnelles, mais il ne peut pas y en avoir
 *   plus d'une. Une redirection doit être suivie d'un token non spécial (le 
 *   fichier source/destination).
 *   - Le '|' désigne le tube ou chaînage de commandes. Le tube doit avoir des
 *   commandes à gauche et à droite (pas de `"abc |"` ou de `"| def"`), et la
 *   commande précédente ne doit pas contenir de redirection (pas de 
 *   `"abc < test | def"`)
 * 
 * Le but du module est de lire une chaîne de caractère et d'appliquer les
 * règles ci-dessus afin d'extraire les composantes importantes de la commande.
 *
 * La commande elle-même est stockée dans l'enregistrement ::cmdline, qui
 * regroupe les informations utiles : redirections (in et out), mise en arrière-
 * plan (backgrounded) et liste des commandes (fragments).
 *
 * La liste des commandes contient chacune des commandes entre les pipes, dans
 * l'ordre d'exécution. Ainsi, la commande :
 * ```
 * abc | def | ghi
 * ```
 * aura pour fragments `abc`, `def` puis `ghi`.
 *
 * Les "fragments" (les sous-commandes) sont stockés dans une liste chaînée,
 * ::cmdfragment. Chaque fragment contient la commande sous forme d'un tableau
 * de chaînes de caractères terminé par NULL (que l'on peut passer à execv*
 * directement) et un pointeur sur le fragment suivant, potentiellement NULL
 * s'il s'agit du dernier.
 *
 * La librairie dispose d'un mécanisme de gestion d'erreur à base de valeurs de
 * retour, suivant l'énumération cmderror. La fonction cmd_errstring permet de
 * récupérer un message en français associé à l'erreur donnée.
 *
 *
 * ## Utilisation typique :
 *
 * ```c
 * char* line;
 * cmdline cmd;
 * cmderror err;
 * cmdfragment* frag;
 * ...
 * line = readline("> ");
 * err = readcmd(line, &cmd);
 *
 * if (err != cmdNoError) {
 *     // Traitement d'erreur
 * } else {
 *     if (cmd->backgrounded) ...
 *     if (cmd->in) ...
 *     frag = cmd->fragments;
 *     while (frag != NULL) {
 *         // Traitement
 *         frag = frag->next;
 *     }
 *     cmd_dispose(cmd); // Pour éviter les fuites !
 * }
 * ```
 *
 */
#ifndef READCMD_H
#define READCMD_H

#include <stdbool.h>

/**
 * Énumération contenant les codes d'erreurs suscpetibles d'être générés par la
 * librairie.
 * À noter que les codes d'erreur sont tous négatif (sauf l'absence d'erreur)
 * suivant la convention pour la plupart des bibliothèques.
 */
enum cmderror {
    cmdNoError              =  0,       //!< Pas d'erreur
    cmdUnescapable          = -1,       //!< Caractère inéchappable
    cmdUnendedQuote         = -2,       //!< Guillemet jamais fermé
    cmdMisplacedAmp         = -3,       //!< `&` au mauvais endroit
    cmdMultipleAmp          = -4,       //!< Plusieurs `&`
    cmdInvalidInRedirect    = -5,       //!< Redirection `<` mal placée
    cmdMultipleInRedirects  = -6,       //!< Plusieurs `<`
    cmdInvalidOutRedirect   = -7,       //!< Redirection `>` mal placée
    cmdMultipleOutRedirects = -8,       //!< Plusieurs `>`
    cmdEmptyPipe            = -9,       //!< Tube avec une commande vide
    cmdInvalidEmptyCmd      = -10,      //!< Commande vide innattendue
    cmdInvalidRedirect      = -11,      //!< Redirection au mauvais endroit
    cmdUnexpectedQuote      = -12,      //!< Guillemet mal placé
};
typedef enum cmderror cmderror;  //!< Typedef pour éviter les `enum` partout

/**
 * Résultat de la lecture d'une commande, obtenu avec readcmd.
 *
 * Lorsqu'un champ n'est pas présent, il est mit à NULL, excepté backgrounded,
 * qui est à faux par défaut.
 *
 * En particulier, si la commande est vide (i.e. l'entrée de readcmd était vide
 * ou remplie d'espaces), alors le champ fragments est à NULL.
 */
struct __cmdline {
    char* in;                           //!< Redirection d'entrée
    char* out;                          //!< Redirection de sortie
    bool backgrounded;                  //!< Mise en arrière-plan
    struct cmdfragment* fragments;      //!< Liste des commandes tubées
};
typedef struct __cmdline* cmdline;  //!< Typedef pour éviter les `**`

/**
 * Liste des commandes enchaînées sous la forme d'une liste chaînée.
 *
 * La commande est découpée en tableau de chaînes de caractères, se terminant
 * par un NULL.
 *
 * Par exemple, la commande `ls -al ~/test` est stockée sous la forme
 * `{"ls", "-al", "~/test", NULL}`, et est donc prête à passer au exec.
 */
struct cmdfragment {
    char** command;             //!< Commande du fragment
    struct cmdfragment* next;   //!< Prochain fragment (ou NULL si fini)
};
typedef struct cmdfragment cmdfragment; //!< Typedef pour éviter les `struct`

// Création/Destruction de cmdline
/**
 * Fonction principale; traite une chaîne de caractère pour en extraire un
 * objet cmdline.
 *
 * L'argument cmd sert de sortie à la fonction (en plus de sa valeur de retour).
 * Il doit s'agir d'un pointeur sur une variable allouée dans la pile. La
 * fonction se charge d'allouer la mémoire pour contenir l'objet cmdline.
 *
 * Une fois terminé, on peut (doit) libérer la mémoire à l'aide de cmd_dispose.
 *
 * En cas d'erreur, un code d'erreur est retourné. Dans ce cas là, le contenu de
 * cmd N'EST PAS SPÉCIFIÉ. Il ne faut donc pas l'utiliser, sous peine de générer
 * des bugs voire des crash.
 *
 * Pré-conditions: input != NULL, cmd != NULL
 * Post-conditions: *cmd == NULL <=> err != ::cmdNoError
 *
 * @param input ligne à traiter
 * @param cmd objet à allouer avec le contenu de la ligne
 * @return un code d'erreur le cas échéant ou ::cmdNoError si tout s'est bien
 * passé
 */
cmderror readcmd(char* input, cmdline* cmd);

/**
 * Libère la mémoire prise par un objet cmdline. Doit être effectué dès qu'on a
 * plus besoin de la commande, et avant un nouveau readcmd si on réutilise le
 * même pointeur.
 *
 * @param cmd commande à libérer
 */
void cmd_dispose(cmdline cmd);

// Gestion d'erreur
/**
 * Récupère un message en français associé à un code d'erreur.
 *
 * @param err le code d'erreur
 * @return message associé
 */
char* cmd_errstring(cmderror err);

// Gestion de la commande
/**
 * Teste si la commande retournée par readcmd est "valide" (c'est à dire qu'il
 * n'y a pas eu d'erreur).
 * 
 * @param cmd commande à tester
 * @return vrai si la commande est valide
 */
bool cmd_valid(cmdline cmd);

/**
 * Teste si la commande est vide (c'est-à-dire sans rien à exécuter/sans
 * fragment).
 *
 * @param cmd commande à tester
 * @return vrai si la commande est vide
 */
bool cmd_empty(cmdline cmd);

#endif // READCMD_H


