## Name of Project

NAME = analog_input_reader

## Color for compilating (pink)

COLOR = \0033[1;35m

## List of Directories

INC_DIR = inc
OBJ_DIR = obj
SRC_DIR = src


## Compilating Utilities
# FAST = -Ofast
DEBUG = -g # -fsanitize=address
WARNINGS = -Wall -Wextra# -Werror
FLAGS =  # $(WARNINGS) $(FAST) $(DEBUG)# -D_REENTRANT

INC = $(INC_DIR:%=-I./%)

LIBS = -lm -lspidev-lib

# CC = clang $(FLAGS) $(INC)
CC = gcc $(FLAGS)

## List of Headers and C files 

SRC_FT = analog_input_reader

## List of Utilities

SRC = $(SRC_FT:%=$(SRC_DIR)/%.c)

OBJ = $(SRC:$(SRC_DIR)%.c=$(OBJ_DIR)%.o)

OBJ_DIRS = $(OBJ_DIR)

## Rules of Makefile

all: $(NAME)
	@echo "$(COLOR)$(NAME) \033[100D\033[40C\0033[1;30m[All OK]\0033[1;37m"

$(OBJ_DIRS):
	@mkdir -p $@
	@echo "$(COLOR)$@ \033[100D\033[40C\0033[1;32m[Created]\0033[1;37m"
	@#@echo "$(COLOR)Creating :\t\0033[0;32m$@\0033[1;37m"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(CC) -c $< -o $@
	@echo "$(COLOR)$@ \033[100D\033[40C\0033[1;32m[Compiled]\0033[1;37m"

$(NAME): $(OBJ_DIRS) $(SRC)
	@$(MAKE) -s -j $(OBJ)
	@echo "$(COLOR)Objects \033[100D\033[40C\0033[1;32m[Created]\0033[1;37m"
	@$(CC) $(OBJ)  $(INC) -o $@ $(LIBS)
	@echo "$(COLOR)$(NAME) \033[100D\033[40C\0033[1;32m[Created]\0033[1;37m"

clean:
	@rm -rf $(OBJ_DIR)
	@echo "$(COLOR)Objects \033[100D\033[40C\0033[1;31m[Removed]\0033[1;37m"

fclean: clean
	@rm -f $(NAME)
	@echo "$(COLOR)$(NAME) \033[100D\033[40C\0033[1;31m[Removed]\0033[1;37m"

re: fclean all

run: coffee
	@echo ""
	@echo "$(COLOR)\"$(NAME)\" \033[100D\033[40C\0033[1;32m[Launched]\0033[1;37m"
	@./$(NAME)

define print_aligned_coffee
	@t=$(NAME); \
	l=$${#t};\
	i=$$((8 - l / 2));\
	echo "\0033[1;32m\033[3C\033[$${i}CAnd Your Program \"$(NAME)\" \0033[1;37m"
endef

coffee: all clean
	@echo ""
	@echo "                    {"
	@echo "                 {   }"
	@echo "                  }\0033[1;34m_\0033[1;37m{ \0033[1;34m__\0033[1;37m{"
	@echo "               \0033[1;34m.-\0033[1;37m{   }   }\0033[1;34m-."
	@echo "              \0033[1;34m(   \0033[1;37m}     {   \0033[1;34m)"
	@echo "              \0033[1;34m| -.._____..- |"
	@echo "              |             ;--."
	@echo "              |            (__  \ "
	@echo "              |             | )  )"
	@echo "              |   \0033[1;96mCOFFEE \0033[1;34m   |/  / "
	@echo "              |             /  / "
	@echo "              |            (  / "
	@echo "              \             | "
	@echo "                -.._____..- "
	@echo ""
	@echo ""
	@echo "\0033[1;32m\033[3C          Take Your Coffee"
	$(call print_aligned_coffee)

help:
	@echo "$(COLOR)Options :\0033[1;37m"
	@echo "\033[100D\033[5C\0033[1;32mmake\033[100D\033[10C \033[100D\033[40C\0033[1;31mCreate executable program\0033[1;37m"
	@echo "\033[100D\033[5C\0033[1;32mmake\033[100D\033[10Cclean\033[100D\033[40C\0033[1;31mClean program objects\0033[1;37m"
	@echo "\033[100D\033[5C\0033[1;32mmake\033[100D\033[10Cfclean\033[100D\033[40C\0033[1;31mCall \"clean\" and remove executable\0033[1;37m"
	@echo "\033[100D\033[5C\0033[1;32mmake\033[100D\033[10Cre\033[100D\033[40C\0033[1;31mCall \"fclean\" and make\0033[1;37m"
	@echo "\033[100D\033[5C\0033[1;32mmake\033[100D\033[10Ccoffee\033[100D\033[40C\0033[1;31mCall make and \"clean\"\0033[1;37m"


.PHONY: all clean fclean re run coffee