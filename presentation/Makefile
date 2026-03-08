# Compile the C-FLAT Beamer presentation
# Requires: texlive-full (or texlive-latex-extra + texlive-fonts-recommended)
#
# Install on Ubuntu/Debian:
#   sudo apt install texlive-full
#   sudo apt install latexmk
#
# Usage:
#   make          # build PDF (runs pdflatex twice for cross-refs)
#   make clean    # remove auxiliary files
#   make purge    # remove all generated files including PDF

SRC  = cflat_presentation
PDF  = $(SRC).pdf

.PHONY: all clean purge

all: $(PDF)

$(PDF): $(SRC).tex
	pdflatex -interaction=nonstopmode $(SRC).tex
	pdflatex -interaction=nonstopmode $(SRC).tex

clean:
	rm -f $(SRC).aux $(SRC).log $(SRC).nav $(SRC).out \
	      $(SRC).snm $(SRC).toc $(SRC).vrb

purge: clean
	rm -f $(PDF)
