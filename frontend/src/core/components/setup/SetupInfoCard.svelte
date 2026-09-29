<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Device identity read over USB (core, generic). info is
  [firmware, version, chip, name] or null while reading. -->
<script>
  import * as Card from '$lib/components/ui/card'

  let { info = null } = $props()
</script>

<Card.Root>
  <Card.Header class="pb-3">
    <Card.Title class="text-lg">Device infos</Card.Title>
    <Card.Description>Read from device.</Card.Description>
  </Card.Header>
  <Card.Content>
    {#if info}
      <table class="w-full text-sm">
        <tbody>
          <tr class="border-t">
            <td class="p-2 text-muted-foreground">Firmware</td>
            <td class="p-2 font-mono">{info[0] ?? '—'}</td>
          </tr>
          <tr class="border-t">
            <td class="p-2 text-muted-foreground">Version</td>
            <td class="p-2 font-mono">{info[1] ?? '—'}</td>
          </tr>
          <tr class="border-t">
            <td class="p-2 text-muted-foreground">Chip</td>
            <td class="p-2 font-mono">{info[2] ?? '—'}</td>
          </tr>
          <tr class="border-t">
            <td class="p-2 text-muted-foreground">Name</td>
            <td class="p-2 font-mono">{info[3] ?? '—'}</td>
          </tr>
        </tbody>
      </table>
    {:else}
      <p class="text-sm text-muted-foreground">Reading…</p>
    {/if}
  </Card.Content>
</Card.Root>
