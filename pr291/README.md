# PR291 uploader helper checks

Candidate `08348c4aaa4e7ed75f6228efc4405c71831638fe`; parent `a39802903355def990ae50d36d64ea3fadfea653`. Four actual Linux uploader + Morton decoder cases pass with Werror: immutable aligned slices/guest rewrite, ring exhaustion, capacity-growth branch, failed-flush ownership. Vulkan/VMA capacity, submission and completion are doubles. Broken slice reuse, publication after failed flush and missing HOST barrier mutations fail. No native GPU, allocation replacement, save/load or performance qualification. Linux-only target and mandatory phase order were independently reviewed.

Expected flush error -5 is injected; it is not a native VMA failure. Checkpatch: zero errors, one new-file MAINTAINERS reminder.
