FROM espressif/idf:release-v5.3

# Narzędzia do dokumentacji
RUN apt-get update && apt-get install -y --no-install-recommends \ 
    doxygen graphviz &&     rm -rf /var/lib/apt/lists/*

# Ułatwienia
ENV IDF_CCACHE_ENABLE=1
