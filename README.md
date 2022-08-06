# KeyScanner
_BTC32 Private Key Experiments_

These Probably aren't the Droids You're Looking For.

Speaking of looking for things... I was thinking on the [BTC32 Puzzle](https://privatekeys.pw/faq#puzzle) about alternate searching algorithms, potential weaknesses in the RNG, alternative GPU solutions as alternatives to the scalar point multiplcation, ML solutions for discoverying patterns or anti-patterns that could lead to deterministic brute force exploits.

Motivation & considerations:

Myself, I have an unrecoverable wallet with a fraction of a btc from ~2012.  Laptop hard drive failure.  Attempted a clean-room-ish physical reconstruction of the drive but many sectors were corrupted in the failure.

POSIT:
Many of the abandoned wallets on the blockchain were based on Private Keys generated from very early openSSL (.98h) builds.

Theory:
Could there be exploits to recover my key based on BN_Rand_Range and contemporary platform limitations?

Test:
I generated a billion keys using from-the-era hardware and OSs and did analysis looking for grouping anomalies in the set.   Even early openSSL         builds seem to have done a good job of stirring the pool between requests and using perfcounters to approximate random seeds and re-stirring parms.

CONCLUSION: I don't think there is anything here.  My BTC private key from that time is lost.  At least it wasn't life-changing money.

_Into the Looking Glass_

During the course of the above experiment, I did run into some interesting behaviors in the bn_rand_range when the range is a fraction of the order of G.  Call it a.. overwrap that seemed to defeat the distribution uniforminity, somewhat.  Researching this behavior, I ran into the BTC32 Puzzle Transactions.   A set of deterministically generated private keys, funded with a fraction of BTC equal to the bitlength of the key.  As of the time of this writing, the 64bit puzzle block hasn't been solved after (years) of effort in the community.  The 0 and 5 keys have exposed public keys, which open up Pollard Rho, Baby-Step/Giant-Step exploits but keys 64,66,67,68,69,71 and so fourth remain pure bruteforce metrics for algorithm testing.

The point of this project is to build out flexible test scenarios to poke at the Puzzle 64 problem.  Puzzle 64 is a key in the range of 8000000000000000:ffffffffffffffff.  18,438,744,073,709,551,615 keys are in the range. Assuming a single RTX 3090 could solve at 1.8 Billion Keys per second, it would take that card 118,561 days to traverse the keyspace.  Almost 325 years!  If you had 255 cards running with no hashrate lost and collaborative overlap, it would take 1.27 years.  You'd spend $275k in electricity for the chance to crack 0.64BTC in a ~year and it gets exponentially more difficult with each bit length in difficulty.

Imagine the security implications of this wasn't the case, though.  I'm not just referring to btc, either.   ECC brute force shortcuts would have a tremendous impact on information security and you better believe this is an area of study for bad actors and white hates, alike.

..and.. that long winded intro is why I'm fascinated by this area of research:  The impact of emerging capabilities of GPUs, Machine Learning, Quantum Stepping algorithms and predictive modeling on communications security.

This is heavily based on experiments from [BitCrack](https://github.com/brichard19/BitCrack) by [brichard19](https://github.com/brichard19) and includes quality of life improvements contributed from [BitCrack2](https://github.com/secp8x32/BitCrack2) by [secpk8x32](https://github.com/secp8x32)

Anything clever here probably came from those guys, show them some love.  Anything hackneyed and bs probably came from me, throwing things against the wall. This is CUDA only, sorry.  My boring real job keeps me pretty busy so multi-platform isn't in the cards for this hobby project atm.

This is highly experimental and fluid, in testing the lower puzzle blocks I definitely see there are some issues to resolve and I'm working through them.

# Theory of Operation

I wanted something Random-based but targetable for this task.   Everytime someone asks for Random-based features, the feature forks I've found were half-measures or got backed out entirely b/c smart-people-concensus was "Random will never work at scale, so don't bother."

What I wanted out of this project was essentially a billion-spins-per-second private-key war dialer with configurable directives. 
My thought was more along the line of a random-assisted combing action than a comprehensive stepping-leapfrog action of the original Bitcrack and VanitySearch projects.   Their performance breakthrough (IMO) was the genius CUDA implementation of the EC scalar multiplication.   Any straight random-number based scenarios I could come up with would loose some of that performance but I wanted to write experiments directly trying to explot the distributive qualities of RNGs.    Search the keys between keys... Using distribution constraints as a keymap for directed random searches.

It seems like Random Start Points iterating by Masked Random Strides sort-of fit the picture of what I wanted, so here we are.

I'm a jack-of-all-trades / master-of-none, so .. be patient with me. ¯\_(ツ)_/¯


## Note: This is experimental & operates differently than the original projects.  
Some of these experimental approaches drop original functionality.
Don't use this to attempt theft.  You won't be successful and it isn't worth the cost to your character.
I just find the code-breaking aspect of the BTC32 Puzzle to be super fascinating and this explores random and mixed sequential-random search techniques.

# Changes 
Starting Points & Incrementor Changes:

Bitcrack originally generated points sequentially from the beginning of your range up to an exponent buffer size deteremined by the concurrency factor ([streaming processor blocks] x [threads] x [points]).   It seeds the gpu with an iterator based on that same concurrency factor and the stride.    On each iteration, Bitcrack would add the interator point to the starting key and keep cycling until end of keyspace or end of time.   

Let's say your starting parameters ended in a buffer size of 2,048,000 starting points.   The first start point would be [start-of-range] and the last start point would be ([start-of-range] + (stride x 2,048,000).   On the first interation, the 1st start point is now [last start point] +1 and the last start point is now [last_start_point] + [last_start_point]  + (stride x 2,048,000).

Keystepper modifies the starting exponent generation process to generate starting points randomly (with some assistance from a buffer populated by thrust) and increments those by the stride specified. You aren't searching the range sequentially but staking initial random search points and incrementing them by the stride on each iteration.

This is represented by the [cycles] output.  Cycles are just iterations.  

# KeyMasks are used to guide key generation
```
[n]       - sequential number 
[r]       - random number
0-9/a-f   - literals
```
as an example:
```
KeyMask: 8nnnrrrrrrrrnnnn
```

In this case, each key would be constructed from 4 parts:
A literal: 8
A wrap-around sequential number from 0-4096 or 000:fff hex.
Two blocks of random numbers from 0000:ffff each generated from deeper random pools of unsigned ints (A matrix 16 columns wide by [totalpoints /2]
A number randomly selected from a shallow pool from 0-65536 int  (0000:ffff)

Sequential pools are built through straight iteration until full.  So [nnnn] would be 0-65536 as ints or [0000] through [ffff] as hex.
Every alternating sequential block (block mod 2==0) is treated differently in a sort of tik-tok between sequentials and random pool selection.  Evens are laid down sequentially as the keys are built and Odds are chosen at random from the underlying sequential pool.  
Reason being: I wanted to have at least a few keys where the first[nnnn] and the last[nnnn] were probably good matches for the key we are searching for and then all our hopes are on the middle two [rrrr] blocks (in the case of 64).

These are built through uint256 expansion through multiplication and addition, so it is pretty performant though complicated masks of alternating literals and sequentials can slow down creation.  

That example would return something like this:
```
8001[some randoms]76CD
8002[some randoms]9FD1
```
.. and so on. 

Key generation for randoms is pretty fast thanks to thrust but the final key assembly is stil CPU-Bound.
Sequentials can slow things down with too many consecutive [n]'s.  Hell it might even oferflow, I haven't tried too hard to break it.

```
[2022-06-23.14:15:03] [Info] Starting at : 9900000000000000 (64 bit)
[2022-06-23.14:15:03] [Info] Ending at   : 99FFFFFFFFFFFFFF (64 bit)
[2022-06-23.14:15:03] [Info] Range       : FFFFFFFFFFFFFF (56 bit)
[2022-06-23.14:15:03] [Info] Stride      : 1000
[2022-06-23.14:15:03] [Info] Key Mask  : 99nnrrrrrrrrnnnn
[2022-06-23.14:15:03] [Info] Stopping after: 1024 cycles
[2022-06-23.14:15:03] [Info] Counting by : 1000 (13 bit)
[2022-06-23.14:15:03] [Info] Initializing NVIDIA GeForce RTX 3070
[2022-06-23.14:15:03] [Info] NViDiA Compute Capability: 46 (8 cores)
[2022-06-23.14:15:03] [Info] Concurrent Key Capacity: 6,029,312  (230.0MB)
[2022-06-23.14:15:03] [Info] Start Key: 9900000000000000
[2022-06-23.14:15:03] [Info] End Key: 99FFFFFFFFFFFFFF
[2022-06-23.14:15:03] [Info] Parsing Key Mask: 99nnrrrrrrrrnnnn
[2022-06-23.14:15:03] [Info] ----------------------------------------------
[2022-06-23.14:15:03] [Info] KeyMask Entry: nn
[2022-06-23.14:15:03] [Info] Sequential Entries Generated: 256 (Linear sorted)
[2022-06-23.14:15:03] [Info] ----------------------------------------------
[2022-06-23.14:15:03] [Info] ----------------------------------------------
[2022-06-23.14:15:03] [Info] KeyMask Entry: rrrrrrrr
[2022-06-23.14:15:03] [Info] Expander Basis: 100000000
[2022-06-23.14:15:03] [Info] Generating 48,234,496 Random Buffer Pool (GPU-Assisted)
[2022-06-23.14:15:04] [Info] 48,234,496 16bit Random Buffer Entries Generated in 325ms
[2022-06-23.14:15:04] [Info] Assembling Random Buffer in Range: 10000000:FFFFFFFF
[2022-06-23.14:15:04] [Info] Generated 6,029,313 Random Buffer Entries in 565ms
[2022-06-23.14:15:04] [Info] ----------------------------------------------
[2022-06-23.14:15:04] [Info] ----------------------------------------------
[2022-06-23.14:15:04] [Info] KeyMask Entry: nnnn
[2022-06-23.14:15:05] [Info] Sequential Entries Generated: 65,536 (Random sorted)
[2022-06-23.14:15:06] [Info] ----------------------------------------------
[2022-06-23.14:15:06] [Info] Consolidating KeyMask Literals...
[2022-06-23.14:15:06] [Info] Seeding Start Keys Vector..
[2022-06-23.14:15:06] [Info] Assembling Start Keys (CPU-Bound)..
[2022-06-23.14:15:06] [Info] Pass #1 Mask: 99 Len: 2
[2022-06-23.14:15:06] [Info] Pass #2 Mask: nn Count(Entries): 256
[2022-06-23.14:15:07] [Info] Pass #3 Mask: rrrrrrrr Count(Entries): 6,029,313
[2022-06-23.14:15:08] [Info] Pass #4 Mask: nnnn Count(Entries): 65,536
[2022-06-23.14:15:08] [Info] SAMPLE Key: 9975BD6312115C7F (753,664)
[2022-06-23.14:15:08] [Info] SAMPLE Key: 99E9D90708F9A84F (1,507,328)
[2022-06-23.14:15:08] [Info] SAMPLE Key: 995EAF83B13F9B0E (2,260,992)
[2022-06-23.14:15:08] [Info] SAMPLE Key: 99D29392939ABE64 (3,014,656)
[2022-06-23.14:15:08] [Info] SAMPLE Key: 994720C746900DC6 (3,768,320)
[2022-06-23.14:15:08] [Info] SAMPLE Key: 99BBFFCB7389680C (4,521,984)
[2022-06-23.14:15:08] [Info] SAMPLE Key: 99309FB653A03AFA (5,275,648)
[2022-06-23.14:15:08] [Info] Total Keys Generated: 6,029,311

```
# StrideQueues and StrideMasks

KeyStepper can try multiple strides from a single run.  A copy of the initial keys are stored on the gpu device and can be device-only swapped back in as a reset during the iterations.   You can specify the stride pattern with a text file of stride masks:

Stride Mask format is: cycles;stride;directive;
Supported Directives:

stride - just denotes a regular stride entry to try
randomstride - denotes a randomly generated stride, based on the stride mask provided
redistribute - shifts the keys by a factor specified in the cycle position x the iteration at runtime
regenerate - regenerates (just the random portions) of the start keys without having to fully reinitialize the gpu

For Example:
```
256;0000000000010000;stride;
256;0000000100000000;stride;
256;0001000000000000;stride;
256;0000000000000100;stride;
256;0000000000010000;stride;
256;0000000000030000;stride;
```
This stride map would tell Keystepper to run each strides for 256 cycles each, then reset the keys between each stride.

A more complicated example with retries and randoms:

```
8;0;repeat;
512;0000xxxxxxxx0000;randomstride:0|1|0|3|0;
512;0000xxxxxxxx0000;randomstride:0-3;
512;0000xxxxxxxx0000;randomstride:0|3;
8;0;endrepeat;
```
This would repeat everything between repeat and endrepeat 8 times. 
randomstride combined with x's in the stride tell Keystepper to randomly make up strides from the range specified in the directive.
(in this case, the x's will be replaced with randomly chosen values from the ranges or sets specified)

0|1|0|3|0 would randomly generate strides including 0,1,3 and favoring 0's with a 3:1 ratio.
0-3 would randomly generate strides including 0,1,2,3 with equal probability distribution.

Back to the code-breaker slot machine mental image, this allows you to control wheel volatility.   More 0's is a less chaotic "spin" per cycle.  More non-zero's is more chaotic "spin" per cycle.

The following example will repeat 8 times and regenerate only the random portions of the keys between each repeat.

```
8;0;repeat;
256;000000000000001;stride;
256;0000000000000100;stride;
256;0000000000001000;stride;
256;0000000000010000;stride;
256;0000000000100000;stride;
256;0000000001000000;stride;
256;0000000010000000;stride;
256;0000000100000000;stride;
256;0000001000000000;stride;
256;0000010000000000;stride;
256;0000100000000000;stride;
256;0001000000000000;stride;
256;0010000000000000;stride;
256;0100000000000000;stride;
256;1000000000000000;stride;
256;0000xxxxxxxx0000;randomstride:0-3;
256;0000xxxxxxxx0000;randomstride:0|1|0|1|0;
256;0000xxxxxxxx0000;randomstride:0|1|0|3|0;
256;0000xxxxxxxx0000;randomstride:0|1|0|0|0;
0;0;regenerate;
8;0;endrepeat;
```
Cycles on the stridemask and MaxCycles as a start parameter are used to control how long it will run for a given stride.  As I mentioned earlier, Cycles are interations.   Cycles are more like guidelines than hard rules.  If you specify 256 cycles, KeyFidner.cpp will attempt to shut down that process once you've reached 256 cycles but the GPU is humming along so fast, some threads may extend beyond 256 cycles before the gpu search is stopped or it also could cut-short the search if one thread hits 256 before another.   All this to say, cycles aren't exact so play around with it.  I tend do aim for lower cycle values to keep my GPUs from having to run hot.  It is 105F outside today and my AC unit is old.
 
# Usage
See /samples  - the batch file is what I've been running in search for btc32 puzzl 64.
```
KeyStepper --help

```
BTC32 Puzzle 64 Mask and Stride (Middle-key up to 4 positions)
```
keystepper --keyspace 8000000000000000:8fffffffffffffff --keymask 8nnn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace 9000000000000000:9fffffffffffffff --keymask 9nnn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace a000000000000000:afffffffffffffff --keymask annn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace b000000000000000:bfffffffffffffff --keymask bnnn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace c000000000000000:cfffffffffffffff --keymask cnnn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace d000000000000000:dfffffffffffffff --keymask dnnn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace e000000000000000:efffffffffffffff --keymask ennn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace f000000000000000:ffffffffffffffff --keymask fnnn0000rrrrnnnn --stride 0000000100000000 --maxcycles 65536 -c -b 82 -t 256 -p 128 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt

```
.. takes about 2 minutes per key range portion

BTC32 Puzzle 64 Mask and Stride (Middle-key up to 5 positions)
```
keystepper --keyspace 8000000000000000:8fffffffffffffff --keymask 8nnn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace 9000000000000000:9fffffffffffffff --keymask 9nnn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace a000000000000000:afffffffffffffff --keymask annn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace b000000000000000:bfffffffffffffff --keymask bnnn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace c000000000000000:cfffffffffffffff --keymask cnnn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace d000000000000000:dfffffffffffffff --keymask dnnn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace e000000000000000:efffffffffffffff --keymask ennn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
keystepper --keyspace f000000000000000:ffffffffffffffff --keymask fnnn00000rrrnnnn --stride 0000000010000000 --maxcycles 1048576 -c -b 82 -t 256 -p 64 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
```
.. takes about 25 minutes per key range portion 

BTC32 Puzzle 64 with StrideMask Input File (various stride methods, including random)  8000-8fff block only
```
keystepper --keyspace 8000000000000000:8fffffffffffffff --keymask 8nnnrrrrrrrrnnnn --stride 0000000000001000 --stridemap stridemap64r.txt -c -b 46 -t 256 -p 512 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt

```
Example quick run from 9800-98ff btc32 puzzle 64
```
keystepper --keyspace 9800000000000000:98ffffffffffffff --keymask 98nnrrrrrrrrnnnn --stride 0000000000001000 --maxseconds 0 --maxhrs 0 --stridemap stridemap64r.txt --maxcycles 1024 -c -b 46 -t 256 -p 512 16jY7qLJnxb7CHZyqBP8qca9d51gAjyXQN -o 64-puzzle-out.txt
[2022-06-23.14:11:06] [Info] Compression : compressed
[2022-06-23.14:11:06] [Info] Search mode : ADDRESS
[2022-06-23.14:11:06] [Info] Starting at : 9800000000000000 (64 bit)
[2022-06-23.14:11:06] [Info] Ending at   : 98FFFFFFFFFFFFFF (64 bit)
[2022-06-23.14:11:06] [Info] Range       : FFFFFFFFFFFFFF (56 bit)
[2022-06-23.14:11:06] [Info] Stride      : 1000
[2022-06-23.14:11:06] [Info] Key Mask  : 98nnrrrrrrrrnnnn
[2022-06-23.14:11:06] [Info] Stopping after: 1024 cycles
[2022-06-23.14:11:06] [Info] Counting by : 1000 (13 bit)
[2022-06-23.14:11:06] [Info] Initializing NVIDIA GeForce RTX 3070
[2022-06-23.14:11:06] [Info] NViDiA Compute Capability: 46 (8 cores)
[2022-06-23.14:11:06] [Info] Concurrent Key Capacity: 6,029,312  (230.0MB)
[2022-06-23.14:11:06] [Info] Start Key: 9800000000000000
[2022-06-23.14:11:06] [Info] End Key: 98FFFFFFFFFFFFFF
[2022-06-23.14:11:06] [Info] Parsing Key Mask: 98nnrrrrrrrrnnnn
[2022-06-23.14:11:06] [Info] ----------------------------------------------
[2022-06-23.14:11:06] [Info] KeyMask Entry: nn
[2022-06-23.14:11:06] [Info] Sequential Entries Generated: 256 (Linear sorted)
[2022-06-23.14:11:06] [Info] ----------------------------------------------
[2022-06-23.14:11:06] [Info] ----------------------------------------------
[2022-06-23.14:11:06] [Info] KeyMask Entry: rrrrrrrr
[2022-06-23.14:11:06] [Info] Expander Basis: 100000000
[2022-06-23.14:11:06] [Info] Generating 48,234,496 Random Buffer Pool (GPU-Assisted)
[2022-06-23.14:11:06] [Info] 48,234,496 16bit Random Buffer Entries Generated in 326ms
[2022-06-23.14:11:06] [Info] Assembling Random Buffer in Range: 10000000:FFFFFFFF
[2022-06-23.14:11:07] [Info] Generated 6,029,313 Random Buffer Entries in 595ms
[2022-06-23.14:11:07] [Info] ----------------------------------------------
[2022-06-23.14:11:07] [Info] ----------------------------------------------
[2022-06-23.14:11:07] [Info] KeyMask Entry: nnnn
[2022-06-23.14:11:08] [Info] Sequential Entries Generated: 65,536 (Random sorted)
[2022-06-23.14:11:08] [Info] ----------------------------------------------
[2022-06-23.14:11:08] [Info] Consolidating KeyMask Literals...
[2022-06-23.14:11:09] [Info] Seeding Start Keys Vector..
[2022-06-23.14:11:09] [Info] Assembling Start Keys (CPU-Bound)..
[2022-06-23.14:11:09] [Info] Pass #1 Mask: 98 Len: 2
[2022-06-23.14:11:09] [Info] Pass #2 Mask: nn Count(Entries): 256
[2022-06-23.14:11:10] [Info] Pass #3 Mask: rrrrrrrr Count(Entries): 6,029,313
[2022-06-23.14:11:11] [Info] Pass #4 Mask: nnnn Count(Entries): 65,536
[2022-06-23.14:11:11] [Info] SAMPLE Key: 9875A40ABB8B1D78 (753,664)
[2022-06-23.14:11:11] [Info] SAMPLE Key: 98E938F7E4A075A6 (1,507,328)
[2022-06-23.14:11:11] [Info] SAMPLE Key: 985E98F915929A50 (2,260,992)
[2022-06-23.14:11:11] [Info] SAMPLE Key: 98D275A419E211AD (3,014,656)
[2022-06-23.14:11:11] [Info] SAMPLE Key: 9847DFC6F519513E (3,768,320)
[2022-06-23.14:11:11] [Info] SAMPLE Key: 98BBA057736ADC3D (4,521,984)
[2022-06-23.14:11:11] [Info] SAMPLE Key: 9830B01895E48F8A (5,275,648)
[2022-06-23.14:11:11] [Info] Total Keys Generated: 6,029,311
[2022-06-23.14:11:11] [Info] Key Buffer Size: 6,029,312
[2022-06-23.14:11:11] [Info] Writing 6,029,312 keys to NVIDIA GeForce RTX 3070...
[2022-06-23.14:11:11] [Info] 10.0%  20.0%  30.0%  40.0%  50.0%  60.0%  70.0%  80.0%  90.0%  100.0%
[2022-06-23.14:11:12] [Info] Done
[2022-06-23.14:11:12] [Info] Loading strides from 'stridemap64r.txt'
[2022-06-23.14:11:12] [Info] 80 strides loaded
[NVIDIA RTX3070 2/7GB] [S: 964.96 MK/s] [111,916,089,344 (37 bit)] [43/80 strides]  [472 local cycles] [16,277 total cycles] [00:01:54]
'''

## Tip Jar
- BTC: 31pjgrq8esTi9jKUL3S2Y4435MVABK4iHk
- ETH: 0x3cbe8027e756b9fe0cf18dcbbed9efb89b7ad9f0

## __Disclaimer__
THIS IS FOR EDUCATIONAL PURPOSES ONLY. USE IT AT YOUR OWN RISK. THE DEVELOPER WILL NOT BE RESPONSIBLE FOR ANY LOSS, DAMAGE OR CLAIM ARISING FROM USING THIS PROGRAM.
