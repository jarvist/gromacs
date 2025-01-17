Performance improvements
^^^^^^^^^^^^^^^^^^^^^^^^

.. Note to developers!
   Please use """"""" to underline the individual entries for fixed issues in the subfolders,
   otherwise the formatting on the webpage is messed up.
   Also, please use the syntax :issue:`number` to reference issues on GitLab, without the
   a space between the colon and number!

Dynamic pairlist generation for energy minimization
"""""""""""""""""""""""""""""""""""""""""""""""""""

With energy minimization, the pairlist, and domain decomposition when running
in parallel, is now performed when at least one atom has moved more than the
half the pairlist buffer size. The pairlist used to be constructed every step.

Nonbonded free-energy kernels use SIMD
""""""""""""""""""""""""""""""""""""""

Free energy calculation performance is improved by making the nonbonded free-energy
kernels SIMD accelerated. On AVX2-256 these kernels are 4 to 8 times as fast.
This should give a noticeable speed-up for most systems, especially if the
perturbed interaction calculations were a bottleneck. This is particularly the
case when using GPUs, where the performance improvement of free-energy runs is
up to a factor of 3.

:issue:`2875`
:issue:`742`
