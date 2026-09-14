# Ce Makefile est là pour vous aider 
# Vous pouvez le modifier, ajouter des règles, en enlever ...
# Vous pouvez ne pas vous en servir, mais ce serait un tort

# Compilateur a utilliser
CC=gcc 

# Fichier à contruire
EXE=minishell

# Quelles options pour le compilateur ? 
# -> tous les warnings possibles, traités comme des erreurs, respect du std C, ajoute ./include dans le chemin de recherche des fichiers .h 
CFLAGS=-Wall -Wextra -Werror -pedantic -I./include

# Options pour l'édition de liens
# -> ici, recherche des librairies supplémentaires dans ./lib, nécessaire pour libreadcmd
LDFLAGS=-lreadcmd -L./lib -lreadline


# Les fichiers .o nécessaires pour contruire le fichier EXE :
OBJECTS = minishell.o 

all: $(EXE)

# règle de création de minishell.o, $@ représente l'objectif, $< le nom de la première dépendance 
# ici, $@ = minishell.o, $< = minishell.c
minishell.o: minishell.c
	$(CC) $(CFLAGS) -c -o $@ $<

# règle pour la création de minishell $(EXE) == minishell, $^ représente toutes le dépendances, ici minishell.o et libreadcmd.a
$(EXE): $(OBJECTS) 
	$(CC)  $^ -o $@ $(LDFLAGS)

clean:
	\rm -f *.o *~
	\rm -f $(EXE)

archive: clean
	(cd .. ; tar cvf minishell-`whoami`.tar minishell)

help:
	@echo "Makefile for minishell."
	@echo "Targets:"
	@echo " all             Build the minishell"
	@echo " archive	 Archive the minishell"
	@echo " clean           Clean artifacts"