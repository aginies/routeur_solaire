#!/bin/bash
# Script pour gérer le site de documentation VitePress

# Couleurs pour le terminal
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 [commande]"
    echo ""
    echo "Commandes:"
    echo "  dev     Lance le serveur de développement (live reload)"
    echo "  build   Génère le site statique dans docs/.vitepress/dist"
    echo "  preview Prévisualise le build localement"
    echo "  help    Affiche cette aide"
}

case "${1:-dev}" in
    dev)
        echo -e "${BLUE}--- Lancement du serveur de développement VitePress ---${NC}"
        npm run docs:dev
        ;;
    build)
        echo -e "${BLUE}--- Génération du site statique ---${NC}"
        npm run docs:build
        echo -e "${GREEN}--- Build terminé avec succès ! ---${NC}"
        ;;
    preview)
        echo -e "${BLUE}--- Prévisualisation du build ---${NC}"
        npm run docs:preview
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        echo "Erreur: Commande inconnue '$1'"
        usage
        exit 1
        ;;
esac
