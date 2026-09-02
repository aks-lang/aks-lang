# Chapter 10: Advanced Pure Data Structures

High-throughput applications require specialized data structures beyond generic arrays and hash maps. AKS includes native implementations of Fenwick Trees, Dynamic Segment Trees, Disjoint Set Union (DSU), Min-Max Heaps, SkipLists, Treaps, Cuckoo Filters, Hash Array Mapped Tries (HAMT), Suffix Arrays, Wavelet Trees, and Radix Trees.

---

## 10.1 Fenwick Tree (Binary Indexed Tree)

A Fenwick Tree supports point updates and prefix sum queries over an array of numbers in $O(\log N)$ time using bitwise manipulation of least significant set bits (`idx & -idx`):

```aks
let bit = FenwickNewC(10);

// Point updates: FenwickUpdateC(handle, index, value)
FenwickUpdateC(bit, 1, 5);
FenwickUpdateC(bit, 2, 3);
FenwickUpdateC(bit, 3, 7);

// Prefix sum query: FenwickQueryC(handle, index)
let prefix_sum = FenwickQueryC(bit, 3);
print("Prefix sum [1..3]: " + String(prefix_sum)); // 15 (5 + 3 + 7)
```

---

## 10.2 Dynamic Segment Tree

Segment Trees maintain intervals over an array, enabling arbitrary range sum, range minimum, and range maximum queries in $O(\log N)$:

```aks
let seg = SegTreeNewC(8);

SegTreeUpdateC(seg, 0, 10);
SegTreeUpdateC(seg, 1, 20);
SegTreeUpdateC(seg, 2, 30);
SegTreeUpdateC(seg, 3, 40);

// Query sum over range [1..3]
let range_sum = SegTreeQueryC(seg, 1, 3);
print("Range Sum [1..3]: " + String(range_sum)); // 90 (20 + 30 + 40)
```

---

## 10.3 Disjoint Set Union (DSU / Union-Find)

DSU tracks partitioned elements across disjoint subsets, featuring **path compression** and **union by rank** to achieve near-constant amortized time $O(\alpha(N))$:

```aks
let dsu = DSUNewC(10);

DSUUnionC(dsu, 1, 2);
DSUUnionC(dsu, 2, 3);
DSUUnionC(dsu, 4, 5);

print("Are 1 and 3 connected? " + String(DSUSameC(dsu, 1, 3))); // true
print("Are 1 and 4 connected? " + String(DSUSameC(dsu, 1, 4))); // false
```

---

## 10.4 Double-Ended Min-Max Priority Heap

A Min-Max Heap allows retrieving and popping both the **minimum** and **maximum** elements in $O(\log N)$ time from a single tree structure:

```aks
let mm_heap = MinMaxHeapNewC();

MinMaxHeapPushC(mm_heap, 45);
MinMaxHeapPushC(mm_heap, 10);
MinMaxHeapPushC(mm_heap, 90);
MinMaxHeapPushC(mm_heap, 25);

print("Popped Min: " + String(MinMaxHeapPopMinC(mm_heap))); // 10
print("Popped Max: " + String(MinMaxHeapPopMaxC(mm_heap))); // 90
```

---

## 10.5 Probabilistic SkipList

SkipLists use multi-level linked lists with probabilistic height promotion to achieve $O(\log N)$ search, insertion, and deletion without complex tree rebalancing:

```aks
let sl = SkipListNewC();

SkipListInsertC(sl, "port", 8080);
SkipListInsertC(sl, "threads", 16);

print("Contains 'port'? " + String(SkipListContainsC(sl, "port"))); // true
SkipListDeleteC(sl, "port");
print("Contains 'port' after delete? " + String(SkipListContainsC(sl, "port"))); // false
```

---

## 10.6 Randomized Treap (Tree + Heap)

A Treap combines a Binary Search Tree on keys with a Min-Heap on randomly assigned priorities, maintaining logarithmic search tree balance via tree rotations:

```aks
let treap = TreapNewC();

TreapInsertC(treap, 50, "Root Node");
TreapInsertC(treap, 20, "Left Subtree");
TreapInsertC(treap, 80, "Right Subtree");

print("Treap contains key 20? " + String(TreapContainsC(treap, 20))); // true
```

---

## 10.7 Cuckoo Filter

Cuckoo Filters provide probabilistic set membership testing with support for **deletions**, offering higher space efficiency than Bloom filters at low false-positive rates:

```aks
let cuckoo = CuckooFilterNewC(10000);

CuckooFilterInsertC(cuckoo, "user_alice_session");
print("Is Alice logged in? " + String(CuckooFilterContainsC(cuckoo, "user_alice_session"))); // true

CuckooFilterDeleteC(cuckoo, "user_alice_session");
print("Is Alice logged in after logout? " + String(CuckooFilterContainsC(cuckoo, "user_alice_session"))); // false
```

---

## 10.8 Persistent Hash Array Mapped Trie (HAMT)

HAMTs are immutable, 32-way branching persistent search trees. Inserting a key creates a new root node sharing unchanged subtrees with the previous version:

```aks
let map_v1 = HamtMapNewC();
let map_v2 = HamtMapInsertC(map_v1, "config_env", "PRODUCTION");
let map_v3 = HamtMapInsertC(map_v2, "db_port", 5432);

print("v2 config: " + String(HamtMapGetC(map_v2, "config_env"))); // "PRODUCTION"
print("v1 db_port (isolated): " + String(HamtMapGetC(map_v1, "db_port"))); // nil
print("v3 db_port: " + String(HamtMapGetC(map_v3, "db_port"))); // 5432
```

---

## 10.9 Suffix Array & Longest Common Prefix (LCP)

Suffix Arrays store sorted lexicographical suffixes of a string, enabling fast $O(M \log N)$ pattern searching and repeat analysis:

```aks
let corpus = "banana";
let sa = SuffixArrayBuildC(corpus);

// Binary search for substring 'ana'
let match_pos = SuffixArraySearchC(corpus, sa, "ana");
print("Match found at character index: " + String(match_pos)); // 1 or 3

let lcp = SuffixArrayLcpC(sa);
print("LCP Array: " + String(lcp));
```

---

## 10.10 Wavelet Tree & Radix Tree

- **Wavelet Tree (`WaveletTreeNewC`)**: Computes frequency rank queries $\text{rank}_q(v, i)$ and range quantiles over integer sequences in $O(\log \Sigma)$.
- **Radix Tree (`RadixTreeNewC`)**: Compressed prefix trie optimized for fast HTTP routing and URL path matching.

```aks
let radix = RadixTreeNewC();
RadixTreeInsertC(radix, "/api/v1/users", "USER_HANDLER");
RadixTreeInsertC(radix, "/api/v1/orders", "ORDER_HANDLER");

print("Route dispatch: " + String(RadixTreeFindC(radix, "/api/v1/users"))); // "USER_HANDLER"
```

---

## 10.11 Summary & Next Steps

In this chapter, we explored:
1. Fenwick Trees for prefix sums and Segment Trees for range queries.
2. Disjoint Set Union (DSU) with path compression and rank union.
3. Min-Max Priority Heaps for double-ended priority queues.
4. SkipLists and Randomized Treaps for balanced searching.
5. Cuckoo Filters with $O(1)$ item deletion.
6. Persistent HAMTs, Suffix Arrays, Wavelet Trees, and Radix Trees.

Proceed to [Chapter 11: Mathematics, Numerics & Scientific Computing](Chapter%2011%20-%20Mathematics,%20Numerics%20and%20Scientific%20Computing.md) to explore complex arithmetic, FFT, statistics, RNGs, and numerical linear algebra.
