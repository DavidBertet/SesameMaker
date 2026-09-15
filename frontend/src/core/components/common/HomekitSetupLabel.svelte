<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->
<!-- Official-style HomeKit setup label: house icon + 8-digit code over the QR. -->
<script>
  import {
    homekitSetupCodeFromUri,
    homekitSetupCodeLines,
    HOMEKIT_DIGIT_GLYPHS,
    HOMEKIT_DIGIT_STEP,
  } from 'src/core/lib/homekit.js'

  let { setupUri = '', qrSrc = '' } = $props()

  let code = $derived(homekitSetupCodeFromUri(setupUri))
  let lines = $derived(homekitSetupCodeLines(code))
</script>

<div
  class="inline-block w-52 rounded-xl border-4 border-black bg-white p-2 select-none"
  role="img"
  aria-label={code ? `HomeKit setup code ${lines[0]} ${lines[1]}` : 'HomeKit pairing QR code'}
>
  {#if code}
    <div class="flex items-center gap-1.5 px-0.5 pb-1.5">
      <!-- House glyph: official HomeKit setup-label art (filled paths, cf. homekit-code MIT
       , after maximkulkin/esp-homekit gen_qrcode). -->
      <svg viewBox="0 0 130 120" class="size-14 shrink-0" fill="black" aria-hidden="true">
        <path
          d="m128.28 49.26-14.16-11.3v-20c0-1.46-.57-1.9-1.6-1.9h-8.94c-1.2 0-1.93.24-1.93 1.9v10L67.81 1.3a4.22 4.22 0 0 0-6.09 0L1.31 49.26c-2.13 1.67-1.53 4.1.83 4.1h11.14v61.1c0 2.77.83 4.34 2.6 5.04a7 7 0 0 0 2.72.5h92.43a7.1 7.1 0 0 0 2.72-.5c1.77-.7 2.6-2.27 2.6-5.03V53.33h11.2c2.26 0 2.86-2.4.73-4.07ZM20.66 48.1a8.45 8.45 0 0 1 3.32-6.97c1.7-1.37 37.24-29.03 38.24-29.83a4.42 4.42 0 0 1 2.66-1.14c1 .07 1.95.47 2.7 1.14l38.2 30a8.43 8.43 0 0 1 3.32 6.96v58.9a4.25 4.25 0 0 1-4.72 4.77H25.05a4.2 4.2 0 0 1-4.39-4.77V48.1Z"
        />
        <path
          d="M37.12 99.03H92.4a3.12 3.12 0 0 0 3.32-3.56v-42.4a5.48 5.48 0 0 0-2.2-5L66.95 26.82c-.58-.5-1.3-.78-2.06-.8-.75.03-1.46.31-2.03.8l-26.6 21.23a5.46 5.46 0 0 0-2.2 5v42.4a3.14 3.14 0 0 0 3.07 3.57Zm4.29-43.17A4.08 4.08 0 0 1 42.97 52l19.95-15.94a2.72 2.72 0 0 1 1.7-.66c.63.02 1.24.25 1.72.66.53.47 19.05 15.1 19.95 15.94a4.07 4.07 0 0 1 1.56 3.86V88.2a2.47 2.47 0 0 1-2.69 2.83H44.1a2.45 2.45 0 0 1-2.7-2.83V55.86Z"
        />
        <path
          d="M53.54 80.67h22.44c1 0 1.73-.34 1.73-1.8V60.73a3.34 3.34 0 0 0-1.23-2.67L65.91 50a1.73 1.73 0 0 0-2.3 0l-10.57 8.13a3.33 3.33 0 0 0-1.23 2.67v18.13c0 1.4.73 1.74 1.73 1.74Zm5.92-17.1a1.3 1.3 0 0 1 .53-1.1l4.3-3.34a.8.8 0 0 1 .96 0s4.12 3.33 4.28 3.33a1.3 1.3 0 0 1 .54 1.1v8.57c0 .6-.3.73-.74.73H60.2c-.4 0-.73 0-.73-.73v-8.57Z"
        />
      </svg>
      <!-- Digits: official outline art, no font involved. Row pitch 54 (34 glyph + 20 gap), rows stacked like the printed label. -->
      <div class="flex flex-1 flex-col justify-center gap-1.5">
        {#each lines as row}
          <svg viewBox="0 0 200 48" class="h-auto w-full" fill="black" aria-hidden="true">
            {#each row.split('') as d, i}
              <path
                d={HOMEKIT_DIGIT_GLYPHS[d]}
                transform={`translate(${i * HOMEKIT_DIGIT_STEP} 0)`}
              />
            {/each}
          </svg>
        {/each}
      </div>
    </div>
  {/if}
  {#if qrSrc}
    <img
      src={qrSrc}
      alt="HomeKit pairing QR code"
      class="block aspect-square w-full [image-rendering:pixelated]"
      draggable="false"
    />
  {/if}
</div>
