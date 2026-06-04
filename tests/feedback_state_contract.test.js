const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs/runtime/FEEDBACK_STATE_CONTRACT.md");
const compositionPath = path.join(repoRoot, "vuo-compositions/myworld-feedback-state-proof.vuo");

function cookKeepPreviousFrame(inputs) {
  let toggle = false;
  let bufferA = null;
  let bufferB = null;
  let previousValid = false;
  const outputs = [];

  for (const input of inputs) {
    if (!input.keep || input.image == null) {
      outputs.push({ current: null, previous: null, updated: false, previousValid });
      continue;
    }

    if (toggle) {
      bufferA = input.image;
      outputs.push({ current: bufferA, previous: bufferB, updated: true, previousValid });
    } else {
      bufferB = input.image;
      outputs.push({ current: bufferB, previous: bufferA, updated: true, previousValid });
    }

    toggle = !toggle;
    previousValid = true;
  }

  return outputs;
}

test("FeedbackState contract separates image memory from stateless Texture2D filters", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /FeedbackState := frame-domain image memory across display refreshes/);
  assert.match(source, /TiXL donor node := Lib\.image\.use\.KeepPreviousFrame/);
  assert.match(source, /visible node name := my_KeepPreviousFrame/);
  assert.match(source, /ColorForTextures \/ #9F008A/);
  assert.match(source, /not a stateless texture filter/);
  assert.match(source, /event -> state/);
  assert.match(source, /state -> image/);
  assert.match(source, /CurrentFrame/);
  assert.match(source, /PreviousFrame/);
  assert.match(source, /PreviousFrame` as uninitialized\/invalid until at least two kept frames/);
});

test("FeedbackState contract records TiXL KeepPreviousFrame source behavior", () => {
  const source = fs.readFileSync(contractPath, "utf8");

  assert.match(source, /Operators\/Lib\/image\/use\/KeepPreviousFrame\.cs/);
  assert.match(source, /Operators\/Lib\/image\/use\/KeepPreviousFrame\.t3/);
  assert.match(source, /\.help\/docs\/operators\/lib\/image\/use\/KeepPreviousFrame\.md/);
  assert.match(source, /Keep: bool = true/);
  assert.match(source, /copy/);
  assert.match(source, /toggle/);
  assert.match(source, /do not copy/);
  assert.match(source, /do not toggle buffers/);
  assert.match(source, /do not publish a new current\/previous pair/);
  assert.match(source, /Format\/size change/);
});

test("KeepPreviousFrame double-buffer fixture exposes previous after the second kept frame", () => {
  const outputs = cookKeepPreviousFrame([
    { image: "frameA", keep: true },
    { image: "frameB", keep: true },
    { image: "frameC", keep: false },
    { image: "frameD", keep: true },
  ]);

  assert.deepEqual(outputs[0], {
    current: "frameA",
    previous: null,
    updated: true,
    previousValid: false,
  });
  assert.deepEqual(outputs[1], {
    current: "frameB",
    previous: "frameA",
    updated: true,
    previousValid: true,
  });
  assert.deepEqual(outputs[2], {
    current: null,
    previous: null,
    updated: false,
    previousValid: true,
  });
  assert.deepEqual(outputs[3], {
    current: "frameD",
    previous: "frameB",
    updated: true,
    previousValid: true,
  });
});

test("FeedbackState Vuo proof uses Vuo feedback as host pressure, not exact TiXL parity", () => {
  const source = fs.readFileSync(compositionPath, "utf8");

  assert.match(source, /FeedbackState \/ PreviousFrame host-layer proof/);
  assert.match(source, /FireOnDisplayRefresh \[type="vuo\.event\.fireOnDisplayRefresh"/);
  assert.match(source, /MakeStripeImage \[type="vuo\.image\.make\.stripe"/);
  assert.match(source, /MakeFeedbackTransform \[type="vuo\.transform\.make\.2d"/);
  assert.match(source, /BlendImageWithFeedback \[type="vuo\.image\.feedback"/);
  assert.match(source, /RenderImageToWindow \[type="vuo\.image\.render\.window2"/);
  assert.match(source, /fillcolor="#9F008A"/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MakeStripeImage:refresh/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MakeStripeImage:angle/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> MakeFeedbackTransform:refresh/);
  assert.match(source, /FireOnDisplayRefresh:requestedFrame -> BlendImageWithFeedback:refresh/);
  assert.match(source, /MakeStripeImage:image -> BlendImageWithFeedback:image/);
  assert.match(source, /MakeFeedbackTransform:transform -> BlendImageWithFeedback:feedbackTransform/);
  assert.match(source, /BlendImageWithFeedback:feedbackImage -> RenderImageToWindow:image/);
  assert.match(source, /not exact two-output TiXL parity/);
  assert.doesNotMatch(source, /RenderTarget \[/);
  assert.doesNotMatch(source, /SrvFromTexture2d|UavFromTexture2d|RtvFromTexture2d/);
});
