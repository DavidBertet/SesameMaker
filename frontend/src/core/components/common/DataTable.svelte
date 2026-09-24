<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Generic read-only table: header row, keyed body rows rendered by the
  caller's `children` snippet, and a single empty-state row. Keeps the
  table shell (spacing, headers, empty colspan) in one place.
  `sticky` pins the header (for fixed-height scroll areas); `muted`
  dims the header titles. -->
<script>
  let {
    headers = [],
    rows = [],
    empty = 'No data…',
    rowKey = (row, i) => i,
    sticky = false,
    muted = true,
    children,
  } = $props()
</script>

<table class="w-full text-sm">
  <thead class={sticky ? 'sticky top-0 backdrop-blur' : ''}>
    <tr class={muted ? 'text-left text-muted-foreground' : 'text-left'}>
      {#each headers as header}
        <th class="p-2 font-medium">{header}</th>
      {/each}
    </tr>
  </thead>
  <tbody>
    {#each rows as row, i (rowKey(row, i))}
      {@render children(row, i)}
    {:else}
      <tr>
        <td class="p-4 text-muted-foreground" colspan={headers.length}>{empty}</td>
      </tr>
    {/each}
  </tbody>
</table>
