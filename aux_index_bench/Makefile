CXX      ?= g++
CXXSTD   ?= -std=c++17
CXXFLAGS ?= -O3 $(CXXSTD) -march=native -Wall -Wextra -fno-omit-frame-pointer -pthread
LDFLAGS  ?= -pthread

# ---- External index opt-ins -------------------------------------------------
# Enable an external index adapter by passing WITH_<NAME>=1 on the make
# command line, e.g.
#   make WITH_FASTFAIR=1
#   make WITH_FPTREE=1 WITH_BZTREE=1
# Each external index also needs its source in third_party/<name>/ and its
# include path / extra libraries set below. See third_party/fetch_indexes.sh.
EXTRA_CXX :=
EXTRA_LD  :=
EXTRA_SRC :=

ifeq ($(WITH_FASTFAIR),1)
  EXTRA_CXX += -DWITH_FASTFAIR -Ithird_party/fastfair/concurrent/src
endif
ifeq ($(WITH_WBTREE),1)
  EXTRA_CXX += -DWITH_WBTREE -Ithird_party/wbtree
  # btree.c must be compiled as REAL C (separate object). Passing it to
  # g++ via EXTRA_SRC compiles it as C++, mangles bt_init/bt_intcmp, and
  # breaks the adapter's extern "C" declarations at link time.
  EXTRA_OBJ += wbtree_c.o
endif
ifeq ($(WITH_FPTREE),1)
  EXTRA_CXX += -DWITH_FPTREE -Ithird_party/fptree
  EXTRA_SRC += third_party/fptree/fptree.cpp
  EXTRA_LD  += -ltbb
  # Add -DPMEM and -lpmemobj -lpmem here if you intentionally use FPTree PMDK mode.
endif
ifeq ($(WITH_BZTREE),1)
  EXTRA_CXX += -DWITH_BZTREE -Ithird_party/bztree -DDESC_CAP=16 -DMAX_FREEZE_RETRY=1 -DENABLE_MERGE=0
  EXTRA_SRC += third_party/bztree/bztree.cc
  EXTRA_LD  += -lpmwcas -lnuma -lrt
endif
ifeq ($(WITH_LBTREE),1)
  EXTRA_CXX += -DWITH_LBTREE -DAIB_LBTREE_NO_MAIN -Ithird_party/lbtree/common -Ithird_party/lbtree/lbtree-src -mrtm -mclwb
  # LB+-Tree's concurrency control is Intel TSX/RTM. On CPUs where TSX
  # is disabled (microcode-disabled on most Cascade Lake), _xbegin()
  # always aborts and the tree spins forever right after pool init.
  # LBTREE_NO_RTM=1 makes transactions no-ops — safe ONLY because the
  # harness serialises lbtree behind a global lock (thread_safe=false).
  # build_all.sh sets this automatically when /proc/cpuinfo lacks 'rtm'.
  ifeq ($(LBTREE_NO_RTM),1)
    EXTRA_CXX += -DLBTREE_NO_RTM
  endif
  EXTRA_SRC += third_party/lbtree/lbtree-src/lbtree.cc third_party/lbtree/common/mempool.cc third_party/lbtree/common/nvm-common.cc
  EXTRA_LD  += -lpmem
  # Add -DNVMPOOL_REAL if you want LB+-Tree to map a real PMEM file.
endif
ifeq ($(WITH_UTREE),1)
  EXTRA_CXX += -DWITH_UTREE -Ithird_party/utree
  ifeq ($(UTREE_NO_GPERFTOOLS),1)
    EXTRA_CXX += -Ithird_party/utree_compat
  else
    EXTRA_LD  += -lprofiler
  endif
  # Add -DUSE_PMDK and -lpmemobj if you want uTree PMDK mode.
endif
ifeq ($(WITH_CIRCTREE),1)
  EXTRA_CXX += -DWITH_CIRCTREE
endif
ifeq ($(WITH_DPTREE),1)
  EXTRA_CXX += -DWITH_DPTREE -Ithird_party/dptree/include -Ithird_party/stx-btree/include -Ithird_party/dptree/misc -fpermissive -mrtm
  ifeq ($(DPTREE_NO_GPERFTOOLS),1)
    EXTRA_CXX += -Ithird_party/utree_compat
  else
    EXTRA_LD  += -ltcmalloc
  endif
  EXTRA_SRC += third_party/dptree/src/util.cpp third_party/dptree/src/ART.cpp third_party/dptree/src/art_idx.cpp third_party/dptree/src/MurmurHash2.cpp third_party/dptree/src/bloom.c
  EXTRA_LD  += -ltbb -ltcmalloc_minimal
endif
ifeq ($(WITH_NBTREE),1)
  EXTRA_CXX += -DWITH_NBTREE
endif

ALL_HEADERS := \
  index.hpp hash_index.hpp hash_key_gen.hpp workload.hpp perfctr.hpp \
  mpmc_queue.hpp histogram.hpp traffic_model.hpp op_mix.hpp \
  index_iface.hpp index_factory.hpp \
  adapters/builtin_btree_adapter.hpp adapters/builtin_hash_adapter.hpp \
  adapters/fastfair_adapter.hpp adapters/wbtree_adapter.hpp \
  adapters/fptree_adapter.hpp   adapters/bztree_adapter.hpp \
  adapters/lbtree_adapter.hpp   adapters/utree_adapter.hpp \
  adapters/circtree_adapter.hpp adapters/dptree_adapter.hpp \
  adapters/nbtree_adapter.hpp

bench: bench.cpp $(ALL_HEADERS) $(EXTRA_OBJ)
	$(CXX) $(CXXFLAGS) $(EXTRA_CXX) bench.cpp $(EXTRA_SRC) $(EXTRA_OBJ) -o bench $(LDFLAGS) $(EXTRA_LD)

wbtree_c.o: third_party/wbtree/btree-rtm/btree.c
	$(CC) -O3 -march=native -fno-omit-frame-pointer -c $< -o $@

skew_check: run_skew_check.cpp hash_key_gen.hpp
	$(CXX) -O3 $(CXXSTD) -march=native run_skew_check.cpp -o skew_check

all: bench skew_check

clean:
	rm -f bench skew_check *.o results/*.csv results/*.png

.PHONY: clean all
