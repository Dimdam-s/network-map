CC = gcc
# Include local (headers projet + raylib qui est maintenant dans include/)
CFLAGS = -Wall -Wextra -Iinclude

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Liste des fichiers sources
SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/network_scan.c $(SRC_DIR)/icmp_scanner.c $(SRC_DIR)/device_utils.c $(SRC_DIR)/gui.c

# Génération des noms de fichiers objets correspondants
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Bibliothèques pour Raylib (Lien statique depuis lib/)
LIBS = -Llib -l:libraylib.a -lGL -lm -lpthread -ldl -lrt -lX11

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
	$(CC) $(OBJS) -o $@ $(LIBS)

# Compilation des fichiers sources
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyage
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean directories
