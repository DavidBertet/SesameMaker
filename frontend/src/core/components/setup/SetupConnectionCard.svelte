<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Live USB session header (core, generic). -->
<script>
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { Button } from '$lib/components/ui/button'
  import { PlugZap } from 'lucide-svelte'

  let { portLabel = '', busy = false, onRefresh = () => {}, onDisconnect = () => {} } = $props()
</script>

<Card.Root>
  <Card.Header class="pb-3">
    <Card.Title class="text-lg flex items-center gap-2">
      <PlugZap class="size-4" />
      Device connection
    </Card.Title>
    <Card.Description>
      Connected {portLabel} — unplug or use Disconnect to release the port.
    </Card.Description>
  </Card.Header>
  <Card.Content class="flex flex-wrap items-center gap-3">
    <Badge variant="success">CONNECTED</Badge>
    <Button size="sm" variant="outline" onclick={onRefresh} disabled={busy}>Refresh infos</Button>
    <Button size="sm" variant="ghost" onclick={onDisconnect}>Disconnect</Button>
  </Card.Content>
</Card.Root>
