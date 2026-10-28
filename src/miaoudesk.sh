#!/bin/sh
while :; do
  printf '\033[2J\033[H'
  printf '\033[1;35m'
  cat <<'BANNER'
  __  __ _                         ____  ____
 |  \/  (_) __ _  ___  _   _      / __ \/ ___|
 | |\/| | |/ _` |/ _ \| | | |____| |  | \___ \
 | |  | | | (_| | (_) | |_| |____| |__| |___) |
 |_|  |_|\__, |\___/ \__,_|     \____/|____/
           |___/
BANNER
  printf '\033[0m\n'
  printf '  MiaouOS 0.1  |  MiaouCore  |  x86_64\n'
  printf '  ────────────────────────────────────────────────────────────\n\n'
  printf '       /\_/\\       Bienvenue dans MiaouDesk !\n'
  printf '      ( o.o )      Votre Linux, construit depuis les sources.\n'
  printf '       > ^ <\n\n'
  printf '  [1] Informations système\n'
  printf '  [2] Terminal\n'
  printf '  [3] Miaou Store (gestionnaire de paquets)\n'
  printf '  [4] Redémarrer\n'
  printf '  [5] Éteindre\n\n'
  printf '  Sélectionnez une option (1-5) : '
  read choice
  case "$choice" in
    1)
      printf '\nMiaouOS 0.1\nKernel: '
      uname -r
      printf 'Userland: BusyBox compilé statiquement\n\nAppuyez sur Entrée pour revenir.'
      read pause
      ;;
    2)
      printf '\nTerminal MiaouOS. Tapez exit pour revenir.\n'
      /bin/sh
      ;;
    3)
      /usr/bin/miaou
      ;;
    4)
      reboot -f
      ;;
    5)
      poweroff -f
      ;;
    *)
      ;;
  esac
done
