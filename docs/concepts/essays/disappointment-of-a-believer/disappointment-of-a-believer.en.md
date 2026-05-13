# Disappointment of a Believer

For thirty-five years I believed a simple thing:
if one thread writes data, another thread can read it correctly.

It did not feel like a theory or a hypothesis.
It just felt like how computers work.
So obvious that nobody even questioned it.

It turns out this is not always true.

---

## What We Want

Two threads. One writes, one reads. They run on different cores of the same CPU.

The writer periodically updates some state — for example, sensor readings.
The reader periodically samples that state.

Intermediate values may be lost. We only care about the **latest state**.

But one requirement is strict:

**The reader must never see a torn snapshot.**

No half old and half new data. Only a consistent state.

Three more conditions:

### Bounded Execution Time

Both threads run in a real-time system.

Each operation must complete within a known worst-case time.

Not "usually fast", but **guaranteed to finish before the deadline.**

Think of an automotive braking system:
the driver presses the pedal and the system must compute pressure within a few milliseconds. Missing the deadline means failure.

A lock is not acceptable.

If the reader waits for the writer, the writer may be preempted halfway through an update. The reader waits indefinitely. The deadline is gone.

Another tempting idea is retry:

The reader reads the data and then verifies that nothing changed during the read. If it did — retry.

But if the writer keeps updating, the reader may retry indefinitely.

No upper bound means no real-time guarantee.

### Fixed Memory

The number of buffers is fixed in advance.

No dynamic allocation.

### Real SMP

Not a single-core imaginary world.

A real multi-core processor where both threads truly run at the same time.

---

The problem looks trivial.

One writes. One reads.

What could possibly go wrong?

---

## Double Buffering

The obvious solution is two buffers, A and B.

The writer writes into the inactive buffer and then switches a marker to say which buffer is current.

The reader checks the marker and reads from that buffer.

It looks perfectly safe.

While the reader reads A, the writer writes B.
Then the writer switches the marker to B and writes A next time.

Everything seems fine.

Now consider this scenario.

The reader checks the marker and sees buffer A.

Then it gets preempted.

Meanwhile the writer:

* finishes writing B
* marks B as current
* finishes writing A
* marks A as current again

The reader resumes.

It still intends to read A.

But now the writer is already writing A again.

Like two cars trying to park in the same space at the same time — both the reader and the writer operate on the same buffer.

The reader sees partially updated data.

A torn snapshot.

You might argue this is rare.

Yes.

But rare is not a guarantee.

Real-time systems care about **always**, not **usually**.

---

## Triple Buffering

So add a third buffer.

Now the writer always has a free buffer to write without touching the one being read.

This is a classic technique used in graphics and game engines.

But the race window remains.

The reader learns which buffer is current, and between that moment and the moment it actually starts reading, anything can happen.

The writer may publish new values multiple times and eventually return to the same buffer the reader believes is safe.

Three buffers do not solve the problem for the same reason two buffers do not:

there is a gap between **"found the buffer"** and **"started reading"**.

The gap may be only a few CPU cycles long — but it exists.

And **this tiny gap destroys the idea of guaranteed state transfer.**

---

## The Core Problem

In both cases the reader performs two actions:

1. Find the current buffer
2. Read from it

Between those steps there is a gap.

A very small gap — but it exists.

The writer can slip into that gap and start modifying the same buffer.

To close the gap, both actions would have to happen in one indivisible step:
find the buffer and claim it simultaneously.

But finding and claiming involves two independent memory locations:

* the marker saying which buffer is current
* a flag saying "I am reading this"

No CPU instruction can update both at once. CAS comes close, but it only covers one memory location at a time — not two.

This is not a flaw in processor design.

Two cores execute in parallel, and there is no operation that can atomically coordinate two independent memory locations in bounded time.

---

## Claiming First?

What if the reader claims a buffer first and only then checks if it is current?

But what should it claim if it does not yet know which buffer is current?

Claim all buffers? Then the writer cannot proceed and must wait.

Deadline missed.

Try something smarter:

The reader attempts to claim a buffer and checks whether it is still current. If not, release and retry.

But retry means the execution time depends on the writer.

No upper bound.

Deadline missed again.

---

## Message Passing vs State Sharing

The most surprising discovery in this process was not about algorithms.

It was something else.

Computers are very good at transferring **messages**.

Files can be written and later read.
Network messages arrive intact.
Queues transfer data reliably.

Why does this work?

Because ownership is transferred.

First the sender writes.
Then the receiver reads.

Never both at the same time.

Our problem is different.

The reader must be able to read at any moment.

The writer must be able to write at any moment.

And the data must remain consistent.

That combination turns out to be impossible in general.

Computers can reliably transfer messages.

But guaranteed transfer of **state without synchronization** may be impossible.

---

## Mathematicians Proved It

Later I discovered this is not new.

In 1991 Maurice Herlihy proved that some synchronization problems cannot be solved using only operations on independent memory locations without retries or blocking.

This problem belongs to that class.

It is not "we haven't found a solution yet".

It is "provably impossible".

---

## What Can Be Done

So we must choose a compromise.

One option is retry.

In practice this works because writers do not update infinitely fast.

But there is no strict time bound, which may be unacceptable in hard real-time systems.

Another option is restricting the environment.

Pin both threads to one core.

Then the gap disappears because they cannot run simultaneously.

But that is no longer SMP.

The third option is a lock.

A lock solves the problem correctly.

But now the reader may wait for the writer, and deadlines are no longer guaranteed.

---

## Conclusion

I started with a belief that seemed obvious:

a computer can reliably transfer state from one place to another.

It turns out this is only true when both sides synchronize.

If both sides run in parallel, nobody waits, and memory is bounded, guaranteed state transfer may simply not exist.

This is not a bug in an algorithm.

It is not a poor design choice.

It is a limit of what shared-memory systems can do.

And that was the most surprising part.
