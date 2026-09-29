<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Device Settings tab (app): composes the generic core settings cards
  with SesameMaker copy. Card logic lives in src/core/components/settings/. -->
<script>
  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'
  import MqttCard from 'src/core/components/settings/MqttCard.svelte'
  import ZigbeeCard from 'src/core/components/settings/ZigbeeCard.svelte'
  import HomekitCard from 'src/core/components/settings/HomekitCard.svelte'
  import TimeCard from 'src/core/components/settings/TimeCard.svelte'
</script>

<SectionHeader title="Device Settings" subtitle="MQTT bridge, Zigbee, HomeKit and time" />

<MqttCard />
<ZigbeeCard
  description="Join a Zigbee network as plain switches: door, light and remote lockout. Endpoints follow the opener protocol."
  pairingHint="No coordinator nearby? Hold BOOT 3s to open pairing, 10s+ to factory reset. Same actions as the buttons above."
/>
<HomekitCard productName="SesameMaker" accessoryKind="garage door opener" />
<TimeCard />
