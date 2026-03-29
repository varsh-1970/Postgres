# ─────────────────────────────────────────────────────────────
#  pgwebapp_cpp Makefile — C++17 + libpq PostgreSQL Studio
#  Install deps: sudo apt install g++ make libpq-dev
# ─────────────────────────────────────────────────────────────

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
LDFLAGS  = -lpq -lpthread

# Auto-detect libpq include/lib paths via pg_config
PG_CONFIG := $(shell which pg_config 2>/dev/null)
ifdef PG_CONFIG
  PG_INCDIR := $(shell $(PG_CONFIG) --includedir)
  PG_LIBDIR := $(shell $(PG_CONFIG) --libdir)
  CXXFLAGS  += -I$(PG_INCDIR)
  LDFLAGS   += -L$(PG_LIBDIR) -Wl,-rpath,$(PG_LIBDIR)
endif

SRCDIR   = src
BUILDDIR = build
TARGET   = pgwebapp

.PHONY: all clean run deps

all: $(BUILDDIR) $(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Step 1: Build the HTML embedder tool (C++)
$(BUILDDIR)/embed_html: $(SRCDIR)/embed_html.cpp | $(BUILDDIR)
	$(CXX) -std=c++17 -O2 -o $@ $<

# Step 2: Generate html_data.cpp from index.html
$(BUILDDIR)/html_data.cpp: $(BUILDDIR)/embed_html $(SRCDIR)/index.html
	$< $(SRCDIR)/index.html > $@

# Step 3: Compile html_data.cpp
$(BUILDDIR)/html_data.o: $(BUILDDIR)/html_data.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Step 4: Compile main.cpp (includes Database.hpp, HttpServer.hpp, Json.hpp, Utils.hpp)
$(BUILDDIR)/main.o: $(SRCDIR)/main.cpp \
                    $(SRCDIR)/Database.hpp \
                    $(SRCDIR)/HttpServer.hpp \
                    $(SRCDIR)/Json.hpp \
                    $(SRCDIR)/Utils.hpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Step 5: Link
$(TARGET): $(BUILDDIR)/main.o $(BUILDDIR)/html_data.o
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  ✓ Build successful!  →  ./$(TARGET)"
	@echo "  Run: ./$(TARGET) [port]   (default: 8080)"
	@echo ""

clean:
	rm -rf $(BUILDDIR) $(TARGET)

run: $(TARGET)
	./$(TARGET) 8080

deps:
	@echo "Install required packages:"
	@echo "  Ubuntu/Debian : sudo apt install g++ make libpq-dev"
	@echo "  RHEL/Fedora   : sudo dnf install gcc-c++ make postgresql-devel"
	@echo "  Arch Linux    : sudo pacman -S gcc make postgresql-libs"
	@echo "  macOS (brew)  : brew install postgresql && brew link --force libpq"
