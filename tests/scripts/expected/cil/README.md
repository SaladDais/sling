These files are all compiled CIL files generated with a lightly modified version
of the lscript harness in
https://github.com/SaladDais/LSO2-VM-Performance/. All added testcases should
be generated the same way to ensure conformance with LL's codegen.

There are some slight differences in the CIL generated by that harness compared
to the official compiler for sanity's sake:

* `ldc.i4 0` and `ldc.i4 1` are always compiled as `ldc.i4.0` and `ldc.i4.1`.
* Doubles are also serialized in their hex form to preserve
  precision and avoid having separate  code paths for default initializers vs constant initializers.
* User-defined labels are always prefixed with "ul" to prevent collisions with compiler-defined labels.