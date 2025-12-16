CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Liste des fichiers sources
SRCS = $(wildcard $(SRC_DIR)/*.c)
# Génération des noms de fichiers objets correspondants
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
# Nom de l'exécutable final
TARGET = $(BIN_DIR)/network-map

# Règle par défaut
all: directories $(TARGET)

# Création des répertoires nécessaires
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

# Édition de liens
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ -lpthread

# Compilation des fichiers sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyage
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean directories
